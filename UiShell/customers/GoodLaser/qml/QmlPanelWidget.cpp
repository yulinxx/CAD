#include "QmlPanelWidget.h"

#include <QCoreApplication>
#include <QDebug>
#include <QQmlContext>
#include <QQmlError>
#include <QString>
#include <QUrl>

namespace GL
{
    QmlPanelWidget::QmlPanelWidget(const AA::HostContext& context,
        const QString& panelId,
        QWidget* parent)
        : QQuickWidget(parent)
        , m_context(context)
        , m_panelId(panelId)
    {
        // QML 根节点跟随控件尺寸
        setResizeMode(QQuickWidget::SizeRootObjectToView);

        // 最小宽度
        setMinimumWidth(160);

        applyContextProperties();

        setSource(QUrl(QStringLiteral("qrc:/GoodLaser/QmlPanel.qml")));

        if (status() == QQuickWidget::Error)
        {
            for (const QQmlError& error : errors())
                qWarning().noquote() << "GoodLaser: QML panel failed to load:" << error.toString();
        }
    }

    void QmlPanelWidget::setHostContext(const AA::HostContext& context)
    {
        m_context = context;
        applyContextProperties();
    }

    void QmlPanelWidget::retranslate()
    {
        applyContextProperties();
    }

    void QmlPanelWidget::showStatusMessage(const QString& text)
    {
        m_statusText = text;
        emit sigStatusTextChanged();

        if (QQmlContext* ctx = rootContext())
            ctx->setContextProperty(QStringLiteral("msStatusText"), text);
    }

    void QmlPanelWidget::applyContextProperties()
    {
        QQmlContext* qmlContext = rootContext();
        if (!qmlContext)
            return;

        qmlContext->setContextProperty(QStringLiteral("msTheme"), m_context.strThemeId);
        qmlContext->setContextProperty(QStringLiteral("msLanguage"), m_context.strLanguageId);
        qmlContext->setContextProperty(QStringLiteral("msPanelId"), m_panelId);
        qmlContext->setContextProperty(QStringLiteral("msStatusText"), m_statusText);

        QString strTitle;
        QString strBody;
        if (m_panelId.isEmpty())
        {
            strTitle = QCoreApplication::translate("QmlPanel", "QML panel");       // QML 面板
            strBody = QCoreApplication::translate("QmlPanel", "Rendered by Qt Quick"); // 由 Qt Quick 渲染
        }
        else if (m_panelId == QLatin1String("toolbar"))
        {
            strTitle = QCoreApplication::translate("GoodLaser", "GoodLaser Banner");
            strBody = QCoreApplication::translate("GoodLaser", "Select Tool");
        }
        else if (m_panelId == QLatin1String("tree"))
        {
            strTitle = QCoreApplication::translate("GoodLaser", "Tools");
        }
        else if (m_panelId == QLatin1String("log"))
        {
            strTitle = QCoreApplication::translate("GoodLaser", "Properties");
        }
        else if (m_panelId == QLatin1String("status"))
        {
            strTitle = QCoreApplication::translate("GoodLaser", "Ready");
        }
        else if (m_panelId == QLatin1String("clock"))
        {
            strTitle = QCoreApplication::translate("GoodLaser", "Clock");
        }
        else if (m_panelId == QLatin1String("center"))
        {
            strTitle = QCoreApplication::translate("GoodLaser", "GoodLaser main Work Area");
        }

        qmlContext->setContextProperty(QStringLiteral("msTextTitle"), strTitle);
        qmlContext->setContextProperty(QStringLiteral("msTextBody"), strBody);

        qmlContext->setContextProperty(QStringLiteral("msTextHint"),
            QCoreApplication::translate("QmlPanel", "Skin: %1 · Language: %2")
            .arg(m_context.strThemeId, m_context.strLanguageId));
    }
}  // namespace GL