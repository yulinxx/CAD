#pragma once

#include <QHash>
#include <QString>

#include "UiShell/Contract/DialogRegistry.h"

namespace RD
{
    // 对话框注册
    class DialogRegistry final : public IDialogFactoryRegistry
    {
    public:
        void registerFactory(const QString& dialogId, IDialogFactory* factory) override;

        IDialogFactory* find(const QString& dialogId) const override;

        // 释放动作放在 shell 内编译，避免跨 CRT 直接 delete
        void destroy() override;

    private:
        QHash<QString, IDialogFactory*> m_dialogs;
    };
}  // namespace RD
