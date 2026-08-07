/**
 * @file EntityToVertices.cpp
 * @brief 引擎图元 → 渲染顶点转换实现
 *
 * 阶段1 收口：不再直接识别具体 SyEntity 派生类型（SyLine/SyCircle/...），
 * 改为消费 Engine 侧统一边界 emitEntityGeometry 输出的几何原语契约
 * （ISceneGeometrySink），由本地 IncrementalVertexSink 完成离散化。
 * 离散化公式与全量路径（render_c_api_frame.cpp 的 tessellate*）保持一致，
 * 统一参数来自 render/tess_params.h。
 */
#include "EntityToVertices.h"

#include <render/tess_params.h>

#include "Engine2D/Geometry/EntityGeometryEmitter.h"
#include "Engine/SyEntity/SyEntity.h"
#include "Engine/SyEntity/EType.h"

#include <cmath>
#include <cstring>
#include <unordered_map>

namespace
{
    // 离散化参数统一由 render::tess 定义（见 render/tess_params.h），
    // 保证全量路径与增量路径结果一致

    // 颜色转换：Ut::Color → float[4]
    inline void colorToRGBA(const Ut::Color& c, float out[4])
    {
        out[0] = c.r(); out[1] = c.g(); out[2] = c.b(); out[3] = c.a();
    }

    // ==================== 曲线离散化缓存 ====================

    struct CachedVertexData
    {
        uint64_t geometryHash = 0;
        std::vector<render::VertexP3C3> vertices;
        render::PrimitiveType primType = render::PrimitiveType::LineStrip;
    };

    // 按实体 ID 缓存离散化结果，避免非几何变更时重复离散化
    static std::unordered_map<uint64_t, CachedVertexData> s_vertexCache;

    // 计算实体几何参数哈希（基于控制点 + 类型 + 闭合标志）
    // 仅依赖 SyEntity 基类契约接口，不依赖具体派生类型
    uint64_t computeGeometryHash(const Eg::SyEntity* entity)
    {
        uint64_t hash = static_cast<uint64_t>(entity->eType);
        hash ^= static_cast<uint64_t>(entity->bClosed) + 0x9e3779b9 + (hash << 6) + (hash >> 2);

        const size_t cpCount = entity->getControlPointCount();
        if (cpCount > 0 && cpCount < 256)
        {
            std::vector<Ut::Vec2d> pts(cpCount);
            const size_t written = entity->getControlPoints(pts.data(), cpCount);
            for (size_t i = 0; i < written; ++i)
            {
                // 将 double 的位模式直接混入哈希（C++17 兼容写法）
                uint64_t bitsX = 0, bitsY = 0;
                std::memcpy(&bitsX, &pts[i].x(), sizeof(double));
                std::memcpy(&bitsY, &pts[i].y(), sizeof(double));
                hash ^= bitsX + 0x9e3779b9 + (hash << 6) + (hash >> 2);
                hash ^= bitsY + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            }
        }
        return hash;
    }

    // 尝试从缓存命中：几何未变时仅更新颜色，跳过离散化
    bool tryCacheHit(uint64_t entityId, uint64_t geomHash,
        const Ut::Color& color,
        std::vector<render::VertexP3C3>& outVertices,
        render::PrimitiveType& outType)
    {
        auto it = s_vertexCache.find(entityId);
        if (it == s_vertexCache.end() || it->second.geometryHash != geomHash)
            return false;

        // 几何未变，拷贝缓存顶点并更新颜色
        outVertices = it->second.vertices;
        outType = it->second.primType;
        float rgba[4];
        colorToRGBA(color, rgba);
        for (auto& v : outVertices)
        {
            v.cr = rgba[0]; v.cg = rgba[1]; v.cb = rgba[2];
        }
        return true;
    }

    // 存入缓存
    void storeCache(uint64_t entityId, uint64_t geomHash,
        const std::vector<render::VertexP3C3>& vertices,
        render::PrimitiveType primType)
    {
        auto& entry = s_vertexCache[entityId];
        entry.geometryHash = geomHash;
        entry.vertices = vertices;
        entry.primType = primType;
    }

    // 判断图元类型是否值得缓存（曲线类离散化开销大）
    bool isCacheableType(Eg::EType type)
    {
        return type == Eg::EType::BEZIER ||
               type == Eg::EType::BEZIER2 ||
               type == Eg::EType::NURBS ||
               type == Eg::EType::CIRCLE ||
               type == Eg::EType::ARC ||
               type == Eg::EType::ELLIPSE;
    }

    // ==================== 增量路径几何接收器 ====================
    // 消费 emitEntityGeometry 输出的原语，离散化为 VertexP3C3 顶点数组。
    // 离散化公式必须与全量路径 render_c_api_frame.cpp 的 tessellate* 保持严格一致
    // （参数统一来自 render/tess_params.h），确保两条路径渲染结果一致。

    class IncrementalVertexSink : public Eg::ISceneGeometrySink
    {
    public:
        IncrementalVertexSink(std::vector<render::VertexP3C3>& outVertices,
            render::PrimitiveType& outType)
            : m_vertices(outVertices)
            , m_outType(outType)
        {
        }

        bool emitted() const { return m_emitted; }
        bool sawText() const { return m_sawText; }

        void setCurrentEntityId(uint64_t id) override
        {
            (void)id;
        }

        void emitPolyline(const Ut::Vec2d* points, size_t count, bool bClosed,
            const Ut::Color& color) override
        {
            if (!points || count < 2)
                return;
            m_outType = bClosed ? render::PrimitiveType::LineLoop
                : render::PrimitiveType::LineStrip;
            float rgba[4];
            colorToRGBA(color, rgba);
            for (size_t i = 0; i < count; ++i)
                addVertex(points[i].x(), points[i].y(), rgba);
            m_emitted = true;
        }

        void emitPoint(const Ut::Vec2d& position,
            const Ut::Color& color) override
        {
            m_outType = render::PrimitiveType::PointList;
            float rgba[4];
            colorToRGBA(color, rgba);
            addVertex(position.x(), position.y(), rgba);
            m_emitted = true;
        }

        void emitCircle(const Ut::Vec2d& center, double radius,
            const Ut::Color& color) override
        {
            if (radius <= 0)
                return;
            m_outType = render::PrimitiveType::LineLoop;
            float rgba[4];
            colorToRGBA(color, rgba);
            const int segments = render::tess::kCircleSegments;
            for (int i = 0; i < segments; ++i)
            {
                double angle = (2.0 * render::tess::kPi * i) / segments;
                addVertex(center.x() + radius * std::cos(angle),
                    center.y() + radius * std::sin(angle), rgba);
            }
            m_emitted = true;
        }

        void emitArc(const Ut::Vec2d& center, double radius,
            double startAngle, double endAngle,
            const Ut::Color& color) override
        {
            if (radius <= 0)
                return;
            m_outType = render::PrimitiveType::LineStrip;
            float rgba[4];
            colorToRGBA(color, rgba);
            double start = startAngle;
            double end = endAngle;
            if (end < start)
                end += 2.0 * render::tess::kPi;
            const double angleRange = end - start;
            const int segments = render::tess::arcSegments(angleRange);
            for (int i = 0; i <= segments; ++i)
            {
                double t = static_cast<double>(i) / segments;
                double angle = start + t * angleRange;
                addVertex(center.x() + radius * std::cos(angle),
                    center.y() + radius * std::sin(angle), rgba);
            }
            m_emitted = true;
        }

        void emitEllipse(const Ut::Vec2d& center,
            double radiusX, double radiusY,
            double rotation,
            double startAngle, double endAngle,
            bool bFullEllipse,
            const Ut::Color& color) override
        {
            if (radiusX <= 0 || radiusY <= 0)
                return;
            float rgba[4];
            colorToRGBA(color, rgba);
            double start = startAngle;
            double end = endAngle;
            if (bFullEllipse || (start == 0.0 && end == 0.0))
            {
                start = 0.0;
                end = 2.0 * render::tess::kPi;
            }
            if (end < start)
                end += 2.0 * render::tess::kPi;
            const double angleRange = end - start;
            m_outType = bFullEllipse ? render::PrimitiveType::LineLoop
                : render::PrimitiveType::LineStrip;
            const int segments = render::tess::ellipseSegments(angleRange);
            const double cosR = std::cos(rotation);
            const double sinR = std::sin(rotation);
            for (int i = 0; i <= segments; ++i)
            {
                double t = static_cast<double>(i) / segments;
                double angle = start + t * angleRange;
                double lx = radiusX * std::cos(angle);
                double ly = radiusY * std::sin(angle);
                addVertex(center.x() + lx * cosR - ly * sinR,
                    center.y() + lx * sinR + ly * cosR, rgba);
            }
            m_emitted = true;
        }

        void emitText(const Ut::Vec2d& position, const char* text,
            const Ut::Color& color) override
        {
            (void)position; (void)text; (void)color;
            m_sawText = true;
        }

        void emitTextEx(const Ut::Vec2d& position, const char* text,
            const Ut::Color& color, float fontSize, float rotationRad,
            int hAlign, int vAlign) override
        {
            (void)position; (void)text; (void)color;
            (void)fontSize; (void)rotationRad; (void)hAlign; (void)vAlign;
            m_sawText = true;
        }

        void emitImagePlaceholder(const Ut::Vec2d& topLeft,
            const Ut::Vec2d& topRight,
            const Ut::Vec2d& bottomLeft,
            const Ut::Vec2d& bottomRight,
            const Ut::Color& color) override
        {
            m_outType = render::PrimitiveType::LineStrip;
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

        void emitTriangleSoup(const Ut::Vec3f* vertices, size_t vertexCount,
            const Ut::Vec3f* normals, size_t normalCount,
            const Ut::Color& color) override
        {
            // 2D 增量路径不支持三角形网格，忽略（3D 场景走全量路径）
            (void)vertices; (void)vertexCount; (void)normals;
            (void)normalCount; (void)color;
        }

    private:
        void addVertex(double x, double y, const float rgba[4])
        {
            render::VertexP3C3 v;
            v.px = static_cast<float>(x);
            v.py = static_cast<float>(y);
            v.pz = 0.0f;
            v.cr = rgba[0]; v.cg = rgba[1]; v.cb = rgba[2];
            m_vertices.push_back(v);
        }

        std::vector<render::VertexP3C3>& m_vertices;
        render::PrimitiveType& m_outType;
        bool m_emitted = false;
        bool m_sawText = false;
    };
}

// 供外部调用：全量刷新时清空缓存
void clearEntityVertexCache()
{
    s_vertexCache.clear();
}

/**
 * @brief 将单个引擎图元转换为 VertexP3C3 顶点数组
 *
 * 由 Engine 侧 emitEntityGeometry 完成图元分解（UI 不再识别具体派生类型），
 * 本地 IncrementalVertexSink 将原语离散化为顶点。支持类型与全量路径一致；
 * 文本（emitText/emitTextEx）不支持增量路径，返回 false 由调用方走全量刷新。
 *
 * 曲线类图元（Bezier/Bezier2/Nurbs/Circle/Arc/Ellipse）支持离散化缓存：
 * 当实体仅因颜色/选择/图层变更而标记为脏时，直接复用缓存的顶点数据，
 * 跳过昂贵的离散化计算。
 *
 * 输入事件链路：SceneManager::onSceneChanged → RenderViewport2D::onSceneChanged
 *   → scheduleSceneUpdate → updateSceneRender → applyLightRefresh → 此函数
 * 此函数处于渲染数据准备层，不涉及 OpenGL 调用。
 */
bool entityToVertices(const Eg::SyEntity* entity,
    std::vector<render::VertexP3C3>& outVertices,
    render::PrimitiveType& outType)
{
    if (!entity)
        return false;

    const Ut::Color& color = entity->getColor();

    // 缓存检查：曲线类图元先查缓存，命中则仅更新颜色
    const uint64_t entityId = static_cast<uint64_t>(entity->id);
    uint64_t geomHash = 0;
    if (isCacheableType(entity->eType))
    {
        geomHash = computeGeometryHash(entity);
        if (tryCacheHit(entityId, geomHash, color, outVertices, outType))
            return true;
    }

    // 通过 Engine 侧统一边界分解图元，本地 sink 离散化
    IncrementalVertexSink sink(outVertices, outType);
    if (!Eg::emitEntityGeometry(*entity, sink))
        return false;
    // 文本等复杂类型无法走增量路径（全量刷新已由渲染器处理文本渲染）
    if (sink.sawText())
        return false;
    if (!sink.emitted())
        return false;

    if (isCacheableType(entity->eType))
        storeCache(entityId, geomHash, outVertices, outType);
    return true;
}
