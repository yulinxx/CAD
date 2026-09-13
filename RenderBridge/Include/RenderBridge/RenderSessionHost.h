#pragma once
/**
 * @file RenderSessionHost.h
 * @brief 宿主侧 RenderX runtime / surface / session 的生命周期归口
 *
 * 2D 视口与 3D 视口原本各写一遍完全同构的代码：填 RuntimeDesc、填
 * SurfaceDesc、按 runtime → surface → session 的顺序创建、失败时原路回收、
 * 销毁时逆序拆。两边的实际差异只有三处，这里做成参数：
 *
 *   - applicationName（日志里区分是哪个视口）
 *   - enableDepth    （3D 靠深度缓冲遮挡，2D 靠排序键，是唯一的 Surface 差异）
 *   - clearColor     （2D 是浅灰纸面，3D 是深色背景）
 *
 * 不放进 UICommon：UICommon 的定位是「UI 通用件」，不应该为了这个再去
 * 公共依赖 RenderX；RenderBridge 才是宿主与渲染 DLL 之间的那一层。
 */

#include "RenderBridge/RenderBridgeAPI.h"

#include "render/renderx.h"

#include <cstdint>

namespace RenderBridge
{
    class RENDERBRIDGE_API RenderSessionHost
    {
    public:
        /// 创建参数：只暴露两个视口真正不同的部分，其余走同一套默认
        struct Config
        {
            const char* applicationName = "CAD Viewport";
            uint32_t width = 0;
            uint32_t height = 0;
            bool enableDepth = false;
            uint64_t transientBufferBytes = 64ull * 1024 * 1024;
            float clearColor[4]{ 0.94f, 0.94f, 0.94f, 1.0f };
        };

        RenderSessionHost() = default;
        ~RenderSessionHost();

        RenderSessionHost(const RenderSessionHost&) = delete;
        RenderSessionHost& operator=(const RenderSessionHost&) = delete;

        /**
         * @brief 按 runtime → surface → session 的顺序创建
         *
         * 调用前 GL 上下文必须已当前（surface 走 ForeignGlContext，
         * 需记录当前帧缓冲）。任一步失败都会回收已建好的部分并返回 false，
         * 不会留下半个运行时。
         */
        bool initialize(const Config& config);

        /// 逆序销毁 session → surface → runtime；幂等，可重复调用
        void shutdown();

        bool isReady() const;

        Render::RT::RuntimeHandle runtime() const { return m_runtime; }
        Render::RT::SessionHandle session() const { return m_session; }
        Render::RT::SurfaceHandle surface() const { return m_surface; }

        /// 把后端能力打一条日志；tag 是调用方前缀（两个视口前缀不同）
        void logCapabilities(const char* tag) const;

    private:
        Render::RT::RuntimeHandle m_runtime{ Render::RT::RuntimeHandle::Invalid };
        Render::RT::SurfaceHandle m_surface{ Render::RT::SurfaceHandle::Invalid };
        Render::RT::SessionHandle m_session{ Render::RT::SessionHandle::Invalid };
    };
}  // namespace RenderBridge
