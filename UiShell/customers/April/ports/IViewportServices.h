#pragma once

namespace Apr
{
    // 组合根就绪查询：接线只需知道 2D 文档 / 操作总线 / UI 服务是否已装配。
    // 只暴露布尔查询，不向 UiShell 返回 SceneDocument2D / OperationBus / UiServices 等原始对象。
    // 实现由 Apr::CompositionRootAdapter 从 ApplicationCompositionRoot 适配。
    class IViewportServices
    {
    public:
        virtual ~IViewportServices() = default;

        virtual bool hasSceneDocument2D() const = 0;
        virtual bool hasOperationBus() const = 0;
        virtual bool hasUiServices() const = 0;
    };
}  // namespace Apr
