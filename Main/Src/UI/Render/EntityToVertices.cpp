/**
 * @file EntityToVertices.cpp
 * @brief 引擎图元 → 渲染顶点转换实现
 *
 * 阶段1 收口：不再直接识别具体 SyEntity 派生类型（SyLine/SyCircle/...），
 * 改为消费 Engine 侧统一边界 emitEntityGeometry 输出的几何原语契约
 * （ISceneGeometrySink），由本地 IncrementalVertexSink 完成离散化。
 * 离散化公式与全量路径（render_c_api_frame.cpp 的 tessellate*）保持一致，
 * 统一参数来自 Render::tess（UI/Common/Include/Render/RenderTypes.h）。
 */
#include "EntityToVertices.h"

#include "Engine2D/Geometry/EntityGeometryEmitter.h"
#include "Engine2D/Render/Tessellator.h"
#include "Engine/SyEntity/SyEntity.h"
#include "Engine/SyEntity/EType.h"

#include <cmath>
#include <cstring>
#include <unordered_map>
#include <shared_mutex>

namespace
{
    // 离散化参数统一由 Render::tess 定义（UI/Common/Include/Render/RenderTypes.h），
    // 保证全量路径与增量路径结果一致

    // 曲线离散化复用缓冲：entityToVertices 会被 parallelForIndex 并行调用，
    // thread_local 保证各线程互不干扰，同时预热后不再每次原语都堆分配。
    // 曲线离散化公式统一在 Eg::Tessellator 的固定段数实现里，本文件不再自行重写。
    thread_local Eg::Tessellator::FixedCurvePoints t_curvePoints;

    // 颜色转换：Ut::Color → float[4]
    inline void colorToRGBA(const Ut::Color& c, float out[4])
    {
        out[0] = c.r();
        out[1] = c.g();
        out[2] = c.b();
        out[3] = c.a();
    }

    // ==================== 曲线离散化缓存 ====================

    struct CachedVertexData
    {
        uint64_t geometryHash = 0;
        std::vector<Render::VertexP3C3> vertices;
        Render::PrimitiveType primType = Render::PrimitiveType::LineStrip;
    };

    // 按图元 ID 缓存离散化结果，避免非几何变更时重复离散化
    // 使用 shared_mutex 保护：读操作可以并发，写操作独占
    static std::unordered_map<uint64_t, CachedVertexData> s_vertexCache;
    static std::shared_mutex s_cacheMutex;

    // 计算图元几何参数哈希（基于控制点 + 包围盒 + 类型 + 闭合标志）
    // 仅依赖 SyEntity 基类契约接口，不依赖具体派生类型
    uint64_t computeGeometryHash(const Eg::SyEntity* entity)
    {
        uint64_t hash = static_cast<uint64_t>(entity->eType);
        hash ^= static_cast<uint64_t>(entity->bClosed) + 0x9e3779b9 + (hash << 6) + (hash >> 2);

        // 将单个 double 的位模式混入哈希（C++17 兼容写法）
        auto mix = [&hash](double v) {
            uint64_t bits = 0;
            std::memcpy(&bits, &v, sizeof(double));
            hash ^= bits + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        };

        // 控制点：Line/Polygon 完整；曲线类（Circle/Arc/Ellipse/Bezier/NURBS）仅返回 basePoint。
        const size_t cpCount = entity->getControlPointCount();
        if (cpCount > 0 && cpCount < 256)
        {
            std::vector<Ut::Vec2d> pts(cpCount);
            const size_t written = entity->getControlPoints(pts.data(), cpCount);
            for (size_t i = 0; i < written; ++i)
            {
                mix(pts[i].x());
                mix(pts[i].y());
            }
        }

        // 曲线类控制点接口不完整（半径/角度/控制点未覆盖），仅靠控制点哈希会导致
        // 修改半径/角度/控制点后命中陈旧缓存、曲线仍按旧几何显示。追加包围盒角点作为
        // 几何指纹兜底（包围盒由实际几何计算，可覆盖上述参数变化）。
        const Ut::BBox2d bbox = entity->getBbox();
        if (bbox.isValid())
        {
            mix(bbox.minPt.x());
            mix(bbox.minPt.y());
            mix(bbox.maxPt.x());
            mix(bbox.maxPt.y());
        }
        return hash;
    }

    // 尝试从缓存命中：几何未变时仅更新颜色，跳过离散化
    // 使用共享锁允许多线程并发读
    bool tryCacheHit(uint64_t entityId,
        uint64_t geomHash,
        const Ut::Color& color,
        std::vector<Render::VertexP3C3>& outVertices,
        Render::PrimitiveType& outType)
    {
        std::shared_lock<std::shared_mutex> readLock(s_cacheMutex);
        auto it = s_vertexCache.find(entityId);
        if (it == s_vertexCache.end() || it->second.geometryHash != geomHash)
        {
            return false;
        }

        // 几何未变，拷贝缓存顶点并更新颜色
        outVertices = it->second.vertices;
        outType = it->second.primType;
        float rgba[4];
        colorToRGBA(color, rgba);
        for (auto& v : outVertices)
        {
            v.cr = rgba[0];
            v.cg = rgba[1];
            v.cb = rgba[2];
        }
        return true;
    }

    // 存入缓存
    // 写入必须加独占锁：operator[] 在共享锁下执行会触发隐式插入，构成数据竞争
    void storeCache(uint64_t entityId,
        uint64_t geomHash,
        const std::vector<Render::VertexP3C3>& vertices,
        Render::PrimitiveType primType)
    {
        std::unique_lock<std::shared_mutex> writeLock(s_cacheMutex);
        auto& entry = s_vertexCache[entityId];
        entry.geometryHash = geomHash;
        entry.vertices = vertices;
        entry.primType = primType;
    }

    // 判断图元类型是否值得缓存（曲线类离散化开销大）
    bool isCacheableType(Eg::EType type)
    {
        return type == Eg::EType::BEZIER || type == Eg::EType::BEZIER2 || type == Eg::EType::NURBS ||
            type == Eg::EType::CIRCLE || type == Eg::EType::ARC || type == Eg::EType::ELLIPSE;
    }

    // ==================== 增量路径几何接收器 ====================
    // 消费 emitEntityGeometry 输出的原语，离散化为 VertexP3C3 顶点数组。
    // 离散化公式必须与全量路径 render_c_api_frame.cpp 的 tessellate* 保持严格一致
    // （参数统一来自 render/TessParams.h），确保两条路径渲染结果一致。

    class IncrementalVertexSink : public Eg::ISceneGeometrySink
    {
    public:
        IncrementalVertexSink(std::vector<Render::VertexP3C3>& outVertices,
            Render::PrimitiveType& outType,
            const double* cameraCenter = nullptr)
            : m_vertices(outVertices)
            , m_outType(outType)
            , m_cameraCenter(cameraCenter)
        {
        }

        bool emitted() const
        {
            return m_emitted;
        }

        void setCurrentEntityId(uint64_t /*id*/) override {}

        void emitPolyline(const Ut::Vec2d* points, size_t count, bool bClosed, const Ut::Color& color) override
        {
            if (!points || count < 2)
            {
                return;
            }
            m_outType = bClosed ? Render::PrimitiveType::LineLoop : Render::PrimitiveType::LineStrip;
            float rgba[4];
            colorToRGBA(color, rgba);
            for (size_t i = 0; i < count; ++i)
            {
                addVertex(points[i].x(), points[i].y(), rgba);
            }
            m_emitted = true;
        }

        void emitPoint(const Ut::Vec2d& position, const Ut::Color& color) override
        {
            m_outType = Render::PrimitiveType::PointList;
            float rgba[4];
            colorToRGBA(color, rgba);
            addVertex(position.x(), position.y(), rgba);
            m_emitted = true;
        }

        void emitCircle(const Ut::Vec2d& center, double radius, const Ut::Color& color) override
        {
            if (radius <= 0)
            {
                return;
            }
            // 曲线离散化统一走 Eg::Tessellator 的固定段数实现，与全量路径同一份公式
            Eg::Tessellator::tessellateCircleFixed(center, radius, t_curvePoints);
            if (t_curvePoints.points.empty())
            {
                return;
            }
            m_outType = t_curvePoints.closed ? Render::PrimitiveType::LineLoop : Render::PrimitiveType::LineStrip;
            float rgba[4];
            colorToRGBA(color, rgba);
            for (const Ut::Vec2d& p : t_curvePoints.points)
            {
                addVertex(p.x(), p.y(), rgba);
            }
            m_emitted = true;
        }

        void emitArc(
            const Ut::Vec2d& center, double radius, double startAngle, double endAngle, const Ut::Color& color) override
        {
            if (radius <= 0)
            {
                return;
            }
            // 直接使用原始角度差，保留绘制方向（顺时针为负、逆时针为正）
            Eg::Tessellator::tessellateArcFixed(center, radius, startAngle, endAngle, t_curvePoints);
            if (t_curvePoints.points.empty())
            {
                return;
            }
            m_outType = Render::PrimitiveType::LineStrip;
            float rgba[4];
            colorToRGBA(color, rgba);
            for (const Ut::Vec2d& p : t_curvePoints.points)
            {
                addVertex(p.x(), p.y(), rgba);
            }
            m_emitted = true;
        }

        void emitEllipse(const Ut::Vec2d& center,
            double radiusX,
            double radiusY,
            double rotation,
            double startAngle,
            double endAngle,
            bool bFullEllipse,
            const Ut::Color& color) override
        {
            if (radiusX <= 0 || radiusY <= 0)
            {
                return;
            }
            // 整椭圆取 segments 个点并闭合，弧段取 segments+1 个点且开口，由 Tessellator 内部判定
            Eg::Tessellator::tessellateEllipseFixed(
                center, radiusX, radiusY, rotation, startAngle, endAngle, bFullEllipse, t_curvePoints);
            if (t_curvePoints.points.empty())
            {
                return;
            }
            m_outType = t_curvePoints.closed ? Render::PrimitiveType::LineLoop : Render::PrimitiveType::LineStrip;
            float rgba[4];
            colorToRGBA(color, rgba);
            for (const Ut::Vec2d& p : t_curvePoints.points)
            {
                addVertex(p.x(), p.y(), rgba);
            }
            m_emitted = true;
        }

        void emitText(const Ut::Vec2d& /*position*/, const char* /*text*/, const Ut::Color& /*color*/) override
        {
            // 文本不产顶点：世界文本走 RenderWidget::setWorldText →
            // WorldTextQuadBuilder 独立通道，由 SceneRefreshCoordinator::reconcileTexts
            // 驱动，且 TEXT 图元在增量循环里已被跳过，正常不会走到这里。
        }

        void emitTextEx(const Ut::Vec2d& /*position*/,
            const char* /*text*/,
            const Ut::Color& /*color*/,
            float /*fontSize*/,
            float /*rotationRad*/,
            int /*hAlign*/,
            int /*vAlign*/) override
        {
            // 同 emitText
        }

        void emitImagePlaceholder(const Ut::Vec2d& topLeft,
            const Ut::Vec2d& topRight,
            const Ut::Vec2d& bottomLeft,
            const Ut::Vec2d& bottomRight,
            const Ut::Color& color) override
        {
            m_outType = Render::PrimitiveType::LineStrip;
            float rgba[4];
            colorToRGBA(color, rgba);
            // 5个顶点：TL → TR → BR → BL → TL（闭合线框）
            addVertex(topLeft.x(), topLeft.y(), rgba);
            addVertex(topRight.x(), topRight.y(), rgba);
            addVertex(bottomRight.x(), bottomRight.y(), rgba);
            addVertex(bottomLeft.x(), bottomLeft.y(), rgba);
            addVertex(topLeft.x(), topLeft.y(), rgba);
            m_emitted = true;
        }

        void emitTriangles(const Ut::Vec2d* points, size_t count, const Ut::Color& color) override
        {
            if (!points || count < 3)
            {
                return;
            }
            // 色块填充（SyFillRegion）：顶点流每 3 个构成一个三角形，实心绘制。
            // 不足 3 的尾巴丢弃而不是补齐——补齐会画出数据里并不存在的三角形。
            // 与全量路径 RenderSceneBuilder::emitTriangles 保持同一约定。
            const size_t usable = count - (count % 3);
            m_outType = Render::PrimitiveType::TriangleList;
            float rgba[4];
            colorToRGBA(color, rgba);
            for (size_t i = 0; i < usable; ++i)
            {
                addVertex(points[i].x(), points[i].y(), rgba);
            }
            m_emitted = true;
        }

        void emitTriangleSoup(const Ut::Vec3f* /*vertices*/,
            size_t /*vertexCount*/,
            const Ut::Vec3f* /*normals*/,
            size_t /*normalCount*/,
            const Ut::Color& /*color*/) override
        {
            // 2D 增量路径不支持三角形网格，忽略（3D 场景走全量路径）
        }

    private:
        void addVertex(double x, double y, const float rgba[4])
        {
            Render::VertexP3C3 v;
            if (m_cameraCenter)
            {
                v.px = static_cast<float>(x - m_cameraCenter[0]) + static_cast<float>(m_cameraCenter[0]);
                v.py = static_cast<float>(y - m_cameraCenter[1]) + static_cast<float>(m_cameraCenter[1]);
            }
            else
            {
                v.px = static_cast<float>(x);
                v.py = static_cast<float>(y);
            }
            v.pz = 0.0f;
            v.cr = rgba[0];
            v.cg = rgba[1];
            v.cb = rgba[2];
            m_vertices.push_back(v);
        }

        std::vector<Render::VertexP3C3>& m_vertices;
        Render::PrimitiveType& m_outType;
        const double* m_cameraCenter;
        bool m_emitted = false;
    };
}  // namespace

// 供外部调用：图元删除时丢弃其缓存条目
// erase 必须加独占锁，避免与并发读/写产生数据竞争
void eraseEntityVertexCache(uint64_t entityId)
{
    std::unique_lock<std::shared_mutex> writeLock(s_cacheMutex);
    s_vertexCache.erase(entityId);
}

// 供外部调用：全量刷新时清空缓存
// clear 必须加独占锁，避免与并发读/写产生数据竞争
void clearEntityVertexCache()
{
    std::unique_lock<std::shared_mutex> writeLock(s_cacheMutex);
    s_vertexCache.clear();
}

/**
 * @brief 将单个引擎图元转换为 VertexP3C3 顶点数组
 *
 * 由 Engine 侧 emitEntityGeometry 完成图元分解（UI 不再识别具体派生类型），
 * 本地 IncrementalVertexSink 将原语离散化为顶点。支持类型与全量路径一致；
 * 文本（SyText）与位图（SyImage）不产顶点，它们各有独立的渲染通道，调用方
 * 在遍历脏图元时就已跳过这两类，不会走到这里。
 *
 * 曲线类图元（Bezier/Bezier2/Nurbs/Circle/Arc/Ellipse）支持离散化缓存：
 * 当图元仅因颜色/选择/图层变更而标记为脏时，直接复用缓存的顶点数据，
 * 跳过昂贵的离散化计算。
 *
 * 输入事件链路：SceneManager::onSceneChanged → RenderViewport2D::onSceneChanged
 *   → scheduleSceneUpdate → updateSceneRender → applyLightRefresh → 此函数
 * 此函数处于渲染数据准备层，不涉及 OpenGL 调用。
 */
bool entityToVertices(const Eg::SyEntity* entity,
    std::vector<Render::VertexP3C3>& outVertices,
    Render::PrimitiveType& outType,
    const double* cameraCenter)
{
    if (!entity)
    {
        return false;
    }

    const Ut::Color& color = entity->getColor();

    // 缓存检查：曲线类图元先查缓存，命中则仅更新颜色
    const uint64_t entityId = static_cast<uint64_t>(entity->id);
    uint64_t geomHash = 0;
    if (isCacheableType(entity->eType))
    {
        geomHash = computeGeometryHash(entity);
        if (tryCacheHit(entityId, geomHash, color, outVertices, outType))
        {
            return true;
        }
    }

    // 通过 Engine 侧统一边界分解图元，本地 sink 离散化
    IncrementalVertexSink sink(outVertices, outType, cameraCenter);
    if (!Eg::emitEntityGeometry(*entity, sink))
    {
        return false;
    }
    if (!sink.emitted())
    {
        return false;
    }

    if (isCacheableType(entity->eType))
    {
        storeCache(entityId, geomHash, outVertices, outType);
    }
    return true;
}