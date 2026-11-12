#pragma once
/**
 * @file OffscreenRenderFactory.h
 * @brief 中性入口：创建「不依赖宿主窗口」的离屏渲染工厂
 *
 * 用途仅限独立设备——不需要交换链的场景，比如契约测试、离屏工具。
 * 产品的视口不走这条路，走 `RenderBridge::RenderSessionHost`：表面与场景的
 * 创建依赖宿主侧事实（Qt 拥有 GL 上下文、Metal 交换链挂在宿主 NSView 上），
 * 工厂的 `DeviceConfig` 表达不了。
 *
 * 名字里刻意不带后端字样：调用方（例如应用层单测）只需表达「我要一个独立的
 * 离屏设备」，不需要知道当前构建里究竟是哪个后端在实现它。后端选择与适配
 * 全部留在 RenderBridge 内部。
 *
 * 使用方式：
 *   auto factory = createOffscreenRenderFactory();
 *   auto device = factory->createDevice(config);
 */

#include "RenderBridge/RenderBridgeAPI.h"
#include "RenderAbstraction/IRenderFactory.h"

#include <memory>

namespace RenderBridge
{
    /**
     * @brief 建一个离屏渲染工厂。
     *
     * 返回抽象层接口，调用方看不到具体后端类型，也不需要资源生命周期
     * 之外的任何后端知识。
     */
    RENDERBRIDGE_API std::unique_ptr<RenderAbstraction::IRenderFactory> createOffscreenRenderFactory();
} // namespace RenderBridge
