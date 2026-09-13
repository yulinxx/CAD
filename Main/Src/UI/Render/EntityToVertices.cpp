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

#include <cmath>

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

/**
 * @brief 将单个引擎图元转换为 VertexP3C3 顶点数组
 *
 * 由 Engine 侧 emitEntityGeometry 完成图元分解（UI 不再识别具体派生类型），
 * 本地 IncrementalVertexSink 将原语离散化为顶点。支持类型与全量路径一致；
 * 文本（SyText）与位图（SyImage）不产顶点，它们各有独立的渲染通道，调用方
 * 在遍历脏图元时就已跳过这两类，不会走到这里。
 *
 * 本函数无状态：不做任何跨调用缓存。判断「几何与上一轮是否相同、要不要重新上传」
 * 归 `RenderSceneBuilder` 的段位账本，那一层才持有几何块，也就只有它需要台账。
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

    // 通过 Engine 侧统一边界分解图元，本地 sink 离散化
    IncrementalVertexSink sink(outVertices, outType, cameraCenter);
    if (!Eg::emitEntityGeometry(*entity, sink))
    {
        return false;
    }
    return sink.emitted();
}