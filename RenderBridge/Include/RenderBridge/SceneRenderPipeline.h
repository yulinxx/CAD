#pragma once
/**
 * @file SceneRenderPipeline.h
 * @brief 统一的渲染数据管道：协调几何、文本、位图、覆盖层
 *
 * ## 设计目标
 *
 * 当前渲染数据分散在多个独立的通道中：
 * - 场景几何：RenderSceneBuilder / Mesh3DBuilder → PersistentGeometryStore
 * - 覆盖层：OverlayScene → 瞬态环
 * - 文本：WorldTextQuadBuilder → 瞬态环
 * - 位图：BitmapQuadBuilder → 瞬态环
 * - 场景环境：SceneEnvironment → 瞬态环
 *
 * 本类提供统一的提交入口，将所有渲染数据合成到同一个
 * IRenderScene 中，确保：
 * - 统一的绘制顺序（图元 → 位图 → 文本 → 覆盖层）
 * - 统一的帧生命周期（beginFrame → 提交 → endFrame）
 * - 统一的剔除/排序/合批
 *
 * ## 统一的数据流
 *
 * ```text
 * Document/Scene
 *   → ISceneDataSource::gatherGeometry()
 *     → SceneRenderPipeline::submitGeometry()  // 场景几何
 *     → SceneRenderPipeline::submitOverlay()   // 覆盖层
 *     → SceneRenderPipeline::submitText()      // 文本
 *     → SceneRenderPipeline::submitBitmap()    // 位图
 *     → SceneRenderPipeline::submitSceneEnv()  // 场景环境
 *       → IRenderScene::beginFrame()
 *         → submitDrawList(场景几何)
 *         → submit(transient: 位图/文本/环境)
 *         → submit(transient: 覆盖层)
 *       → IRenderScene::endFrame()
 * ```
 */

#include "RenderAbstraction/IRenderDevice.h"
#include "RenderAbstraction/IRenderScene.h"
#include "RenderBridge/RenderBridgeAPI.h"
#include "RenderBridge/OverlayScene.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace RenderBridge
{
    class PersistentGeometryStore;
    class GeometryPipeline;

    /**
     * @brief 统一的渲染数据提交结构
     *
     * 每帧渲染时，SceneRenderPipeline 将所有渲染数据
     * 汇总到这些容器中，再统一提交。
     */
    struct FrameRenderData
    {
        // 场景几何的 DrawList 句柄（由 GeometryPipeline 管理）
        RenderAbstraction::DrawListHandle geometryDrawList;

        // 瞬态命令：场景环境、位图、文本、覆盖层
        std::vector<RenderAbstraction::DrawInstruction> transientCommands;

        // 场景环境的 DrawList 句柄（如果有）
        RenderAbstraction::DrawListHandle envDrawList;
    };

    /**
     * @brief 统一的渲染数据管道
     *
     * 协调所有渲染数据源的提交，确保统一的帧生命周期和绘制顺序。
     *
     * ## 使用方式
     *
     * ```cpp
     * SceneRenderPipeline pipeline;
     * pipeline.initialize(device, scene);
     *
     * // 每帧
     * pipeline.beginFrame();
     *
     * // 提交场景几何（每帧一次，或脏时增量更新）
     * pipeline.submitGeometry(geometryDrawList);
     *
     * // 提交瞬态数据（每帧重新生成）
     * pipeline.submitTransient(envCommands, bitmapCommands, textCommands);
     *
     * // 提交覆盖层
     * pipeline.submitOverlay(overlayScene);
     *
     * pipeline.endFrame();
     * ```
     */
    class SceneRenderPipeline
    {
    public:
        SceneRenderPipeline() = default;
        ~SceneRenderPipeline() = default;

        SceneRenderPipeline(const SceneRenderPipeline&) = delete;
        SceneRenderPipeline& operator=(const SceneRenderPipeline&) = delete;

        /**
         * @brief 初始化渲染管道
         *
         * @param device 渲染设备
         * @param scene 渲染场景
         * @return 成功返回 true
         */
        bool initialize(RenderAbstraction::IRenderDevice& device,
                        RenderAbstraction::IRenderScene& scene);

        /**
         * @brief 销毁全部资源
         */
        void shutdown();

        /**
         * @brief 是否已初始化
         */
        bool valid() const { return m_scene != nullptr; }

        /**
         * @brief 开始一帧
         *
         * 必须在提交任何数据之前调用。
         */
        bool beginFrame();

        /**
         * @brief 结束一帧
         *
         * 提交所有数据到 GPU。
         */
        void endFrame();

        /**
         * @brief 设置模型矩阵
         */
        void setModelMatrix(const RenderAbstraction::Matrix4x4* matrix);

        /**
         * @brief 设置相机
         */
        void setCamera(const RenderAbstraction::CameraDesc& camera);

        /**
         * @brief 设置光照（3D）
         */
        void setLighting(const RenderAbstraction::LightingDesc& lighting);

        // ---------- 场景几何 ----------

        /**
         * @brief 提交场景几何绘制列表
         *
         * 场景几何走常驻几何仓 + 保留式绘制列表，
         * 每帧提交一次（或脏时增量更新）。
         *
         * @param drawList 绘制列表句柄
         * @param view 可见范围（用于剔除）
         */
        void submitGeometry(RenderAbstraction::DrawListHandle drawList,
                            const RenderAbstraction::ViewVolume* view);

        // ---------- 瞬态数据 ----------

        /**
         * @brief 提交瞬态绘制命令
         *
         * 瞬态数据走瞬态环，每帧重新生成。
         * 包括：场景环境、位图、文本等。
         *
         * @param commands 瞬态命令列表
         */
        void submitTransient(
            const std::vector<RenderAbstraction::DrawInstruction>& commands);

        // ---------- 覆盖层 ----------

        /**
         * @brief 提交覆盖层
         *
         * 覆盖层走瞬态环，按层合成到瞬态命令中。
         *
         * @param overlay 覆盖层场景
         */
        void submitOverlay(const OverlayScene& overlay);

        /**
         * @brief 提交覆盖层命令（已合成的瞬态命令）
         */
        void submitOverlayCommands(
            const std::vector<RenderAbstraction::DrawInstruction>& commands);

        // ---------- 离屏渲染 ----------

        /**
         * @brief 创建渲染目标
         */
        RenderAbstraction::TextureHandle createRenderTarget(
            uint32_t width, uint32_t height);

        /**
         * @brief 设置渲染目标
         */
        bool setRenderTarget(const RenderAbstraction::RenderTargetBinding& target);

        /**
         * @brief 从纹理读回像素
         */
        bool readPixelsFromTexture(RenderAbstraction::TextureHandle texture,
                                   uint32_t x, uint32_t y,
                                   uint32_t width, uint32_t height,
                                   void* outPixels, uint64_t capacity);

        // ---------- 统计 ----------

        /**
         * @brief 获取帧统计
         */
        RenderAbstraction::FrameStatistics getFrameStatistics() const;

        /**
         * @brief 获取渲染设备
         */
        RenderAbstraction::IRenderDevice* device() const { return m_device; }

        /**
         * @brief 获取渲染场景
         */
        RenderAbstraction::IRenderScene* scene() const { return m_scene; }

    private:
        /**
         * @brief 确保瞬态命令缓冲区有足够的容量
         */
        void ensureTransientCapacity(size_t needed);

        RenderAbstraction::IRenderDevice* m_device = nullptr;
        RenderAbstraction::IRenderScene* m_scene = nullptr;

        // 瞬态命令缓冲（跨帧复用容量）
        std::vector<RenderAbstraction::DrawInstruction> m_transientBuffer;

        // 覆盖层命令缓冲
        std::vector<RenderAbstraction::DrawInstruction> m_overlayBuffer;

        // 几何绘制列表句柄
        RenderAbstraction::DrawListHandle m_geometryDrawList;

        // 是否已开始帧
        bool m_frameStarted = false;
    };

} // namespace RenderBridge
