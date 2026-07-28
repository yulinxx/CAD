#include "Renderer3DFactory.h"

#include "SimpleRenderer3D.h"
#include "RenderWidget3DAdapter.h"
#include "Log/SyLogger.h"

std::unique_ptr<IRenderer3D> Renderer3DFactory::create(RendererType type)
{
    switch (type)
    {
        case RendererType::Simple:
            SY_INFO("[Renderer3DFactory] Creating SimpleRenderer3D (validation chain)");
            return std::make_unique<SimpleRenderer3D>();

        case RendererType::Compatible:
            SY_INFO("[Renderer3DFactory] Creating RenderWidget3DAdapter (compatible chain)");
            return std::make_unique<RenderWidget3DAdapter>();

        case RendererType::None:
        default:
            SY_INFO("[Renderer3DFactory] Creating null renderer");
            return nullptr;
    }
}

std::unique_ptr<IRenderer3D> Renderer3DFactory::createDefault()
{
    SY_INFO("[Renderer3DFactory] Creating default renderer: Compatible chain");
    return create(RendererType::Compatible);
}

Renderer3DFactory::RendererType Renderer3DFactory::fromString(const std::string& name)
{
    if (name == "simple" || name == "Simple")
        return RendererType::Simple;
    if (name == "compatible" || name == "Compatible")
        return RendererType::Compatible;
    if (name == "none" || name == "None")
        return RendererType::None;

    SY_WARNF("[Renderer3DFactory] Unknown renderer type: %s, using default", name.c_str());
    return RendererType::Compatible;
}

std::string Renderer3DFactory::toString(RendererType type)
{
    switch (type)
    {
        case RendererType::Simple:
            return "Simple";
        case RendererType::Compatible:
            return "Compatible";
        case RendererType::None:
            return "None";
        default:
            return "Unknown";
    }
}