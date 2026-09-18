#pragma once
/**
 * @file RenderXAdapter.h
 * @brief RenderX 适配层入口（工厂类）
 *
 * 本头文件完全隔离 renderx.h：调用方只看到抽象层接口。
 *
 * 用途**仅限独立设备**——不需要交换链的场景，比如契约测试、离屏工具。
 * 产品的视口不走这条路，走 `RenderBridge::RenderSessionHost`：表面与场景的
 * 创建依赖宿主侧事实（Qt 拥有 GL 上下文、Metal 交换链挂在宿主 NSView 上），
 * 工厂的 `DeviceConfig` 表达不了。
 *
 * 使用方式：
 *   auto factory = RenderXAdapter::createFactory();
 *   auto device = factory->createDevice(config);
 */

#include "RenderBridge/RenderBridgeAPI.h"
#include "RenderAbstraction/IRenderFactory.h"

namespace RenderBridge {

class RENDERBRIDGE_API RenderXAdapter {
public:
    /**
     * @brief 建工厂。
     *
     * 返回抽象层接口而非具体的 RenderXFactory：后者只有 .cpp 里有定义，
     * 调用方拿不到完整类型，unique_ptr 的析构会当场编不过。
     */
    static std::unique_ptr<RenderAbstraction::IRenderFactory> createFactory();

    // 禁止实例化
    RenderXAdapter() = delete;
    RenderXAdapter(const RenderXAdapter&) = delete;
};

} // namespace RenderBridge
