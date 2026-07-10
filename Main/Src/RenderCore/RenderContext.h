#pragma once

#include <cstdint>
#include <string>

#include "RenderCoreApi.h"
#include "RenderTypes.h"

struct RENDER_CORE_API RenderContext
{
    std::string backendName;
    std::string sceneType;

    Size2D viewportSize;

    RenderMode renderMode{ RenderMode::Wireframe };

    bool orbitMode{ true };
    bool measureMode{ false };

    uint64_t frameId{ 0 };
    bool isDirty{ true };

    void markDirty()
    {
        isDirty = true;
    }

    void clearDirty()
    {
        isDirty = false;
    }

    void advanceFrame()
    {
        ++frameId;
    }

    bool is2D() const
    {
        return sceneType == "2D";
    }

    bool is3D() const
    {
        return sceneType == "3D";
    }
};
