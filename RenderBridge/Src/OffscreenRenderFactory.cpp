/**
 * @file OffscreenRenderFactory.cpp
 * @brief 中性离屏工厂入口的实现：委托给当前构建选中的适配层
 *
 * 具体后端只在 RenderBridge 内部出现；调用方通过
 * `RenderBridge/OffscreenRenderFactory.h` 拿到只含抽象层接口的工厂。
 */

#include "RenderBridge/OffscreenRenderFactory.h"

#include "RenderBridge/RenderXAdapter.h"

namespace RenderBridge
{
    std::unique_ptr<RenderAbstraction::IRenderFactory> createOffscreenRenderFactory()
    {
        return RenderXAdapter::createFactory();
    }
} // namespace RenderBridge
