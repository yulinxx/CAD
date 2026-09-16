#pragma once
/**
 * @file RenderSessionHost.h
 * @brief 宿主侧 RenderX runtime / surface / session 的生命周期归口
 *
 * 2D 视口与 3D 视口原本各写一遍完全同构的代码：填 RuntimeDesc、填
 * SurfaceDesc、按 runtime → surface → session 的顺序创建、失败时原路回收、
 * 销毁时逆序拆。两边实际不同的部分都做成参数：
 *
 *   - applicationName（日志里区分是哪个视口）
 *   - enableDepth    （3D 靠深度缓冲遮挡，2D 靠排序键，是唯一的 Surface 差异）
 *   - clearColor     （2D 是浅灰纸面，3D 是深色背景）
 *   - backend        （OpenGL / Metal，见下）
 *   - nativeWindow   （Metal 需要的 NSView*，OpenGL 不使用）
 *
 * 后端的窗口契约完全不同，是这里唯一的分叉：
 *   - OpenGL：surface 走 ForeignGlContext——上下文由 Qt 的 QOpenGLWidget 拥有，
 *     DLL 只记录当前帧缓冲，因此不传任何窗口句柄，并需要 glGetProcAddress
 *     由宿主提供（Qt 的符号解析，见 qtGlGetProcAddress 注释）。
 *   - Metal：CAMetalLayer 必须挂在宿主自建的 layer-hosting NSView 上
 *     （Renderx 的 metalDevice.mm 会建一个独立子视图，不去改 Qt 的视图层级），
 *     因此必须传 NSView*；GL 符号解析器对 Metal 无意义，留空。
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
            /// 渲染后端。默认 OpenGL，保持既有调用方零改动。
            Render::RT::Backend backend = Render::RT::Backend::OpenGL;
            /**
             * 原生窗口句柄。
             *
             * Metal 下必须是 NSView*（转成 void* 以免公共头文件引入 ObjC）；
             * 该视图只需在 initialize 期间存活，DLL 会往它上面挂一个自建的
             * 宿主子视图，之后不再回访它。OpenGL 忽略此字段（上下文由 Qt 拥有）。
             */
            void* nativeWindow = nullptr;
        };

        RenderSessionHost() = default;
        ~RenderSessionHost();

        RenderSessionHost(const RenderSessionHost&) = delete;
        RenderSessionHost& operator=(const RenderSessionHost&) = delete;

        /**
         * @brief 按 runtime → surface → session 的顺序创建
         *
         * 后端按 Config::backend 分叉（见文件头注释）：
         *   - OpenGL：调用前 GL 上下文必须已当前（surface 走 ForeignGlContext，
         *     需记录当前帧缓冲）。
         *   - Metal：必须提供 Config::nativeWindow（NSView*），且该视图已挂进
         *     窗口层级（DLL 会在其上挂宿主子视图；未挂窗口时不会报错，但
         *     交换链要等到视图真正上屏后才有有效尺寸）。
         * 任一步失败都会回收已建好的部分并返回 false，不会留下半个运行时。
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
