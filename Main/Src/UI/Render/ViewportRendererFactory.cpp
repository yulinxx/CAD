#include "ViewportRendererFactory.h"

#if BUILD_UI3D
#include "RenderWidget3DAdapter.h"
#endif
#include "Log/SyLogger.h"

std::unique_ptr<IRenderer3D> ViewportRendererFactory::create(RendererType type)
{
    switch (type)
    {
    case RendererType::Compatible:
#if BUILD_UI3D
        SY_INFO("[ViewportRendererFactory] Creating RenderWidget3DAdapter (compatible chain)");
        return std::make_unique<RenderWidget3DAdapter>();
#else
        SY_DEBUG("[ViewportRendererFactory] UI3D disabled, returning null renderer");
        return nullptr;
#endif

    case RendererType::None:
    default:
        SY_DEBUG("[ViewportRendererFactory] Creating null renderer");
        return nullptr;
    }
}

std::unique_ptr<IRenderer3D> ViewportRendererFactory::createDefault()
{
    SY_INFO("[ViewportRendererFactory] Creating default renderer: Compatible chain");
    return create(RendererType::Compatible);
}

ViewportRendererFactory::RendererType ViewportRendererFactory::fromString(const std::string& name)
{
    if (name == "compatible" || name == "Compatible")
    {
        return RendererType::Compatible;
    }
    if (name == "none" || name == "None")
    {
        return RendererType::None;
    }

    SY_WARNF("[ViewportRendererFactory] Unknown renderer type: %s, using default", name.c_str());
    return RendererType::Compatible;
}

std::string ViewportRendererFactory::toString(RendererType type)
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
