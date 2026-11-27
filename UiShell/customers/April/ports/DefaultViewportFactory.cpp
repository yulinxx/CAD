#include "DefaultViewportFactory.h"

#include "RenderViewport2DAdapter.h"
#include "Viewport3DAdapter.h"

namespace Apr
{
    ICadViewport* DefaultViewportFactory::create2D(QWidget* parent)
    {
        return new RenderViewport2DAdapter(parent);
    }

    ICadViewport* DefaultViewportFactory::create3D(QWidget* parent)
    {
        return new Viewport3DAdapter(parent);
    }
}  // namespace Apr
