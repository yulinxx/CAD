#include "UiPanelRegistry.h"

#include <QWidget>

#include "Log/SyLogger.h"

void UiPanelRegistry::registerPanel(const QString& id, PanelFactory factory)
{
    if (id.isEmpty() || !factory)
    {
        return;
    }
    m_factories.insert(id, std::move(factory));
}

QWidget* UiPanelRegistry::createPanel(const QString& id, QWidget* parent)
{
    auto it = m_factories.constFind(id);
    if (it == m_factories.constEnd())
    {
        SY_WARNF("[UiPanelRegistry] Panel not registered: %s", qPrintable(id));
        return nullptr;
    }
    QWidget* widget = it.value()(parent);
    if (!widget)
    {
        SY_WARNF("[UiPanelRegistry] Panel factory returned null: %s", qPrintable(id));
        return nullptr;
    }
    return widget;
}

bool UiPanelRegistry::isPanelRegistered(const QString& id) const
{
    return m_factories.contains(id);
}

QStringList UiPanelRegistry::registeredPanelIds() const
{
    return m_factories.keys();
}