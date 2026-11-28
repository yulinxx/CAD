#include "Assembly/Widgets/WidgetFactoryRegistry.h"

namespace AA
{
    void WidgetFactoryRegistry::registerFactory(const QString& factoryId, IWidgetFactory* factory)
    {
        if (!factoryId.isEmpty() && factory)
            m_factories.insert(factoryId, factory);
    }

    IWidgetFactory* WidgetFactoryRegistry::find(const QString& factoryId) const
    {
        // 未注册返回 nullptr，由调用方决定如何提示缺失工厂。
        return m_factories.value(factoryId, nullptr);
    }

    void WidgetFactoryRegistry::destroy()
    {
        delete this;
    }

    IWidgetFactoryRegistry* createWidgetFactoryRegistry()
    {
        return new WidgetFactoryRegistry;
    }
}  // namespace AA