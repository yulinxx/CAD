#pragma once
/**
 * @file RenderXAdapterInternal.h
 * @brief 把宿主自建的 runtime / surface / session 包成抽象层对象
 *
 * 与 RenderXAdapter.h 的 createFactory() 路径互补：那条路径自己建 Runtime
 * （用于独立的设备/表面/场景），这条路径面向**句柄已由宿主建好**的场景 ——
 * RenderSessionHost 才有 Metal 的进程级共享 Runtime、GL 的宿主符号解析、
 * 「请求退出 + 最后一个视口释放」这套生命周期规则，重新走一遍工厂会把它们
 * 全部绕开。
 *
 * 包出来的对象**不拥有**句柄：runtime / surface / session 的销毁仍归
 * RenderSessionHost，因此它的析构顺序必须排在抽象对象之后。
 *
 * 本头是 RenderBridge 的私有实现头（在 Src/ 下），句柄用 uint64_t 裸值传递，
 * 不向公共 Include/ 暴露 renderx.h。
 */

#include <cstdint>
#include <memory>

namespace RenderAbstraction {
class IRenderDevice;
class IRenderSurface;
class IRenderScene;
} // namespace RenderAbstraction

namespace RenderBridge {

/// runtime 句柄裸值 → 设备对象（Render::RT::RuntimeHandle 裸值）
std::unique_ptr<RenderAbstraction::IRenderDevice> wrapRenderXDevice(uint64_t runtime);

/// runtime + surface 句柄裸值 → 表面对象
std::unique_ptr<RenderAbstraction::IRenderSurface> wrapRenderXSurface(uint64_t runtime, uint64_t surface);

/// runtime + session 句柄裸值 → 场景对象
std::unique_ptr<RenderAbstraction::IRenderScene> wrapRenderXScene(uint64_t runtime, uint64_t session);

} // namespace RenderBridge
