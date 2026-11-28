#pragma once

#include "IViewportServices.h"
#include "DefaultViewportFactory.h"
#include "IPropertyPanelServices.h"

class ApplicationCompositionRoot;   // AprilCore；只在 .cpp include

namespace Apr
{
    // 低耦合 adapter：把 AprilCore::ApplicationCompositionRoot 适配到客户层窄端口。
    // 组合根只见布尔就绪查询；属性面板只见 PropertyModel / IPropertyEditTarget。
    class CompositionRootAdapter : public IViewportServices, public IPropertyPanelServices
    {
    public:
        explicit CompositionRootAdapter(ApplicationCompositionRoot& root);

        bool hasSceneDocument2D() const override;
        bool hasOperationBus() const override;
        bool hasUiServices() const override;

        // IPropertyPanelServices：属性面板只依赖模型 + 编辑目标窄接口
        PropertyModel buildPropertyModel(const QStringList& ids) const override;
        std::shared_ptr<IPropertyEditTarget> createEditTarget(const QStringList& ids) override;

        IViewportFactory* viewportFactory() { return &m_viewportFactory; }
        IPropertyPanelServices* propertyPanelServices() { return this; }

    private:
        ApplicationCompositionRoot* m_root = nullptr;
        DefaultViewportFactory m_viewportFactory;
    };
}  // namespace Apr
