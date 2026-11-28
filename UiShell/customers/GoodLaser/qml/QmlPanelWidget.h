#pragma once

#include <QQuickWidget>

#include "UiShell/Contract/HostContext.h"

namespace GL
{
    // QML 面板容器
    class QmlPanelWidget : public QQuickWidget,
        public AA::IHostContextAware,
        public AA::IRetranslatable,
        public AA::IStatusMessageView
    {
        Q_OBJECT
            Q_PROPERTY(QString msStatusText READ msStatusText NOTIFY sigStatusTextChanged)

    public:
        explicit QmlPanelWidget(const AA::HostContext& context,
            const QString& panelId = QString(),
            QWidget* parent = nullptr);

    public:
        void setHostContext(const AA::HostContext& context) override;
        void retranslate() override;
        void showStatusMessage(const QString& text) override;

        // msStatusText 的 getter（供 Q_PROPERTY 使用）。
        QString msStatusText() const
        {
            return m_statusText;
        }

    signals:
        void sigStatusTextChanged();

    private:
        void applyContextProperties();

    private:
        AA::HostContext m_context;
        QString m_panelId;
        QString m_statusText;
    };
}  // namespace GL
