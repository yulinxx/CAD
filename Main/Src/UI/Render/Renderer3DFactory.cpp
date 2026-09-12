#include "Renderer3DFactory.h"

#if BUILD_UI3D
#include "RenderWidget3DAdapter.h"
#endif
#include "Log/SyLogger.h"

std::unique_ptr<IRenderer3D> Renderer3DFactory::create(RendererType type)
{
    switch (type)
    {
    case RendererType::Compatible:
#if BUILD_UI3D
        SY_INFO("[Renderer3DFactory] Creating RenderWidget3DAdapter (compatible chain)");
        return std::make_unique<RenderWidget3DAdapter>();
#else
        SY_DEBUG("[Renderer3DFactory] UI3D disabled, returning null renderer");
        return nullptr;
#endif

    case RendererType::None:
    default:
        SY_DEBUG("[Renderer3DFactory] Creating null renderer");
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
    if (name == "compatible" || name == "Compatible")
    {
        return RendererType::Compatible;
    }
    if (name == "none" || name == "None")
    {
        return RendererType::None;
    }

    SY_WARNF("[Renderer3DFactory] Unknown renderer type: %s, using default", name.c_str());
    return RendererType::Compatible;
}

std::string Renderer3DFactory::toString(RendererType type)
{
    switch (type)
    {
    case RendererType::Compatible:
        return "Compatible";
    case RendererType::None:
        return "None";
    default:
        return "Unknown";
    }
}
