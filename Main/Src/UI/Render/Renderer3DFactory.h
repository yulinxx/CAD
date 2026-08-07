#pragma once

#include "Render3D/IRenderer3D.h"
#include <memory>
#include <string>

class Renderer3DFactory
{
public:
    enum class RendererType
    {
        Simple,
        Compatible,
        None
    };

public:
    static std::unique_ptr<IRenderer3D> create(RendererType type);
    static std::unique_ptr<IRenderer3D> createDefault();

    static RendererType fromString(const std::string& name);
    static std::string toString(RendererType type);
};
