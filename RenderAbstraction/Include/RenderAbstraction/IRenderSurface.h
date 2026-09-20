#pragma once
/**
 * @file IRenderSurface.h
 * @brief 渲染表面接口
 *
 * 管理渲染目标的抽象接口。
 */
#include "IRenderTypes.h"

namespace RenderAbstraction {

class IRenderSurface {
public:
    virtual ~IRenderSurface() = default;

    virtual void resize(uint32_t width, uint32_t height) = 0;
    virtual bool isValid() const = 0;
    virtual uint32_t width() const = 0;
    virtual uint32_t height() const = 0;
};

} // namespace RenderAbstraction
