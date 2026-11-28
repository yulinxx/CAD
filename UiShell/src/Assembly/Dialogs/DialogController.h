#pragma once

#include <QJsonObject>
#include <QString>

#include "UiShell/Contract/DialogRegistry.h"

class QWidget;

namespace AA
{
    struct ShellServices;

    // 对话框控制器
    class DialogController
    {
    public:
        DialogController(const ShellServices* services, const QString* profileDir);

    public:
        DialogResult openResult(const QString& dialogId,
            QWidget* parent,
            QString* lastMessage = nullptr,
            const QJsonObject& params = {}) const;

        // 处理 params
        QJsonObject resolveParams(const QJsonObject& params) const;

    private:
        const ShellServices* m_shellServices = nullptr;
        const QString* m_strProfileDir = nullptr;
    };
}  // namespace AA
