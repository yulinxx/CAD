#pragma once

#include "UI3D/Render3D/IRenderer3D.h"
#include <memory>
#include <string>

/**
 * @brief 3D 视口渲染器工厂（原 Renderer3DFactory）
 *
 * 【命名说明】它并不创建「渲染后端」，只创建 Qt 3D Widget 包装器 ——
 * 真正的后端（OpenGL / Vulkan / Metal）由 Renderx 的 Runtime 在内部决定。
 * 因此原名 `Renderer3DFactory` 有误导性，2026-09-12 改名为
 * `ViewportRendererFactory`；真正的后端工厂将来落在 RenderBridge::BackendRendererFactory。
 *
 * 目前只有一条真实链路：Compatible（RenderWidget3DAdapter → RenderWidget3D，OpenGL）。
 * 曾经存在的 Simple（软件渲染 SimpleRenderer3D）链路已删除 —— 它在产品运行时没有触发点，
 * 只有测试在用，且自带一套与 RenderWidget3D 冲突的相机键位约定。
 */
class ViewportRendererFactory
{
public:
    enum class RendererType
    {
        Compatible,
        None
    };

public:
    static std::unique_ptr<IRenderer3D> create(RendererType type);
    static std::unique_ptr<IRenderer3D> createDefault();

    static RendererType fromString(const std::string& name);
    static std::string toString(RendererType type);
};
