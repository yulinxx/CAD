#include "Assembly/Dialogs/DialogRegistry.h"

#include <QtGlobal>

namespace AA
{
    void DialogRegistry::registerFactory(const QString& dialogId, IDialogFactory* factory)
    {
        if (dialogId.isEmpty() || !factory)
            return;

        if (m_dialogs.contains(dialogId))
        {
            // 框架层已注册默认工厂后，客户模块的同名 dialogId 允许覆盖。
            qInfo("DialogRegistry: dialog id '%s' overridden (later factory replaces earlier)",
                qPrintable(dialogId));
        }

        m_dialogs.insert(dialogId, factory);
    }

    IDialogFactory* DialogRegistry::find(const QString& dialogId) const
    {
        return m_dialogs.value(dialogId, nullptr);
    }

    void DialogRegistry::destroy()
    {
        delete this;
    }

    IDialogFactoryRegistry* createDialogRegistry()
    {
        return new DialogRegistry;
    }
}  // namespace AA