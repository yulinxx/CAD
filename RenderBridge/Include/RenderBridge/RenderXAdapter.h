#pragma once
/**
 * @file RenderXAdapter.h
 * @brief RenderX 适配层入口（工厂类）
 *
 * 这个文件是UI层唯一需要包含的渲染接口头文件。
 * 它内部包含 RenderXAdapter 的实现，完全隔离 renderx.h。
 *
 * 使用方式：
 *   auto factory = RenderXAdapter::createFactory();
 *   auto device = factory->createDevice(config);
 *   auto scene = factory->createScene(*device);
 */

#include "RenderAbstraction/IRenderFactory.h"

namespace RenderBridge {

class RenderXFactory;
class RenderXDeviceAdapter;
class RenderXSurfaceAdapter;
class RenderXSceneAdapter;

class RenderXAdapter {
public:
    static std::unique_ptr<RenderXFactory> createFactory();

    // 禁止实例化
    RenderXAdapter() = delete;
    RenderXAdapter(const RenderXAdapter&) = delete;
};

} // namespace RenderBridge
