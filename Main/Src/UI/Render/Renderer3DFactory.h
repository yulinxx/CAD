#pragma once

#include "Render3D/IRenderer3D.h"
#include <memory>
#include <string>

/**
 * @brief 3D 渲染器工厂
 *
 * 目前只有一条真实链路：Compatible（RenderWidget3DAdapter → RenderWidget3D，OpenGL）。
 * 曾经存在的 Simple（软件渲染 SimpleRenderer3D）链路已删除 —— 它在产品运行时没有触发点，
 * 只有测试在用，且自带一套与 RenderWidget3D 冲突的相机键位约定。
 */
class Renderer3DFactory
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
