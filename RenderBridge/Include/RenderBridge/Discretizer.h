#pragma once
/**
 * @file Discretizer.h
 * @brief 曲线离散化：从 RenderSceneBuilder 中分离的独立组件
 *
 * ## 设计目标
 *
 * RenderSceneBuilder::addPiece() 中包含了曲线离散化逻辑
 *（tessellateCircleAdaptive 等），这些逻辑与几何管理完全无关，
 * 但与 RenderSceneBuilder 紧密耦合。
 *
 * 将离散化逻辑提取为独立组件后：
 * - 2D 全量路径和增量路径共用同一份离散化公式
 * - 3D 网格的离散化也使用同一份 Tessellator
 * - 离散化参数（LOD 弦高误差、缩放比例）集中在 Discretizer 中管理
 * - RenderSceneBuilder 只负责几何管理，不关心离散化实现
 *
 * ## 与 Tessellator 的关系
 *
 * 本类不重复实现离散化算法，而是封装 Eg::Tessellator 的静态方法，
 * 提供统一的离散化接口。
 */

#include "Engine/Render/TessParams.h"
#include "Ut/Vec.h"
#include "Ut/Color.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace RenderBridge
{
    /**
     * @brief 离散化输出结果
     */
    struct DiscretizedPoints
    {
        std::vector<Ut::Vec2d> points;  ///< 离散化后的点序列（世界坐标）
        bool closed = false;            ///< 是否闭合
    };

    /**
     * @brief 曲线离散化器
     *
     * 封装 Eg::Tessellator 的静态离散化方法，提供统一的接口。
     * 所有离散化参数（LOD 弦高误差、缩放比例）由本类管理。
     *
     * ## 线程安全
     *
     * tessellateCircle/Arc/Ellipse 使用 thread_local 的
     * FixedCurvePoints 缓冲，与 Tessellator 的线程局部缓存机制一致。
     */
    class Discretizer
    {
    public:
        Discretizer() = default;
        ~Discretizer() = default;

        Discretizer(const Discretizer&) = delete;
        Discretizer& operator=(const Discretizer&) = delete;

        /**
         * @brief 设置 LOD 自适应离散化的目标弦高误差（屏幕像素）
         */
        void setChordErrorPixels(double pixels)
        {
            m_chordErrorPixels = pixels > 0.0 ? pixels : Eg::tess::kLodChordErrorPixels;
        }

        double chordErrorPixels() const { return m_chordErrorPixels; }

        /**
         * @brief 设置世界单位 → 屏幕像素的缩放比例
         */
        void setWorldToScreenScale(double scale)
        {
            m_worldToScreenScale = scale > 0.0 ? scale : 1.0;
        }

        double worldToScreenScale() const { return m_worldToScreenScale; }

        /**
         * @brief 离散化圆
         */
        DiscretizedPoints discretizeCircle(const Ut::Vec2d& center,
                                           double radius) const
        {
            DiscretizedPoints result;
            Eg::Tessellator::FixedCurvePoints fp;
            Eg::Tessellator::tessellateCircleAdaptive(
                center, radius, m_worldToScreenScale, fp, m_chordErrorPixels);
            result.points = std::move(fp.points);
            result.closed = fp.closed;
            return result;
        }

        /**
         * @brief 离散化圆弧
         */
        DiscretizedPoints discretizeArc(const Ut::Vec2d& center,
                                        double radius,
                                        double startAngle,
                                        double endAngle) const
        {
            DiscretizedPoints result;
            Eg::Tessellator::FixedCurvePoints fp;
            Eg::Tessellator::tessellateArcAdaptive(
                center, radius, startAngle, endAngle,
                m_worldToScreenScale, fp, m_chordErrorPixels);
            result.points = std::move(fp.points);
            result.closed = fp.closed;
            return result;
        }

        /**
         * @brief 离散化椭圆/椭圆弧
         */
        DiscretizedPoints discretizeEllipse(const Ut::Vec2d& center,
                                            double radiusX,
                                            double radiusY,
                                            double rotation,
                                            double startAngle,
                                            double endAngle,
                                            bool fullEllipse) const
        {
            DiscretizedPoints result;
            Eg::Tessellator::FixedCurvePoints fp;
            Eg::Tessellator::tessellateEllipseAdaptive(
                center, radiusX, radiusY, rotation,
                startAngle, endAngle, fullEllipse,
                m_worldToScreenScale, fp, m_chordErrorPixels);
            result.points = std::move(fp.points);
            result.closed = fp.closed;
            return result;
        }

        /**
         * @brief 离散化折线（直接使用顶点，不做离散化）
         */
        DiscretizedPoints discretizePolyline(const Ut::Vec2d* points,
                                              size_t count,
                                              bool closed) const
        {
            DiscretizedPoints result;
            result.points.assign(points, points + count);
            result.closed = closed;
            return result;
        }

        /**
         * @brief 离散化三角形集（直接使用顶点）
         */
        DiscretizedPoints discretizeTriangles(const Ut::Vec2d* points,
                                               size_t count) const
        {
            DiscretizedPoints result;
            const size_t usable = count - (count % 3);
            result.points.assign(points, points + usable);
            result.closed = false;
            return result;
        }

        /**
         * @brief 闭合 LineLoop 为 LineStrip（补首点）
         *
         * 放在 Discretizer 而不是各个 emit*：增量路径与全量路径
         * 都经过这里，收在一处就不会再漂移。
         */
        static void closeLineLoop(std::vector<Ut::Vec2d>& points)
        {
            if (points.size() >= 3)
            {
                points.push_back(points.front());
            }
        }

        /**
         * @brief 获取弦高误差目标（屏幕像素）
         */
        static double defaultChordErrorPixels()
        {
            return Eg::tess::kLodChordErrorPixels;
        }

    private:
        double m_chordErrorPixels = Eg::tess::kLodChordErrorPixels;
        double m_worldToScreenScale = 1.0;
    };

} // namespace RenderBridge
