#include "AboutDialog.h"

#include <QDebug>
#include <QDesktopServices>
#include <QKeySequence>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWidget>
#include <QShortcut>
#include <QStringList>
#include <QUrl>
#include <QVBoxLayout>
#include <QtMath>

namespace GL
{
    namespace
    {
        const char* const kQmlSource = "qrc:/GoodLaser/AboutDialog.qml";

        bool isAllowedLink(const QUrl& url)
        {
            const QString scheme = url.scheme().toLower();
            return url.isValid()
                && (scheme == QLatin1String("http") || scheme == QLatin1String("https") || scheme == QLatin1String("mailto"));
        }
    }  // namespace

    AboutDialog::AboutDialog(const QVariantMap& view, QWidget* parent)
        : QDialog(parent)
    {
        setObjectName(QStringLiteral("GoodLaser.about.dialog"));
        setWindowTitle(view.value(QStringLiteral("windowTitle")).toString());

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);

        m_quick = new QQuickWidget(this);
        m_quick->setObjectName(QStringLiteral("GoodLaser.about.quick"));
        m_quick->setResizeMode(QQuickWidget::SizeRootObjectToView);

        m_quick->rootContext()->setContextProperty(QStringLiteral("about"), view);
        m_quick->setSource(QUrl(QString::fromLatin1(kQmlSource)));
        layout->addWidget(m_quick);

        QQuickItem* root = m_quick->rootObject();
        if (m_quick->status() != QQuickWidget::Ready || !root)
        {
            QStringList errors;
            for (const QQmlError& error : m_quick->errors())
                errors.append(error.toString());
            m_loadError = errors.isEmpty() ? QStringLiteral("QML root object was not created")
                : errors.join(QLatin1Char('\n'));
            return;
        }

        // 尺寸
        m_quick->setMinimumSize(qCeil(root->implicitWidth()), qCeil(root->implicitHeight()));

        // QML → C++
        const bool closeOk = connect(root, SIGNAL(closeRequested()), this, SLOT(reject()));
        const bool linkOk = connect(root, SIGNAL(linkActivated(QString)), this, SLOT(openLink(QString)));
        if (!closeOk || !linkOk)
            qWarning("GoodLaser: About dialog QML is missing closeRequested() / linkActivated(string)");

        // Esc / 回车 关闭；
        const QKeySequence closeKeys[] = { QKeySequence(QKeySequence::Cancel), QKeySequence(Qt::Key_Return),
            QKeySequence(Qt::Key_Enter) };
        for (const QKeySequence& keys : closeKeys)
        {
            auto* shortcut = new QShortcut(keys, this);
            connect(shortcut, &QShortcut::activated, this, &QDialog::reject);
        }
    }

    bool AboutDialog::isLoaded() const
    {
        return m_loadError.isEmpty();
    }

    QString AboutDialog::loadError() const
    {
        return m_loadError;
    }

    void AboutDialog::openLink(const QString& url)
    {
        const QUrl target(url, QUrl::StrictMode);
        if (!isAllowedLink(target))
        {
            qWarning().noquote() << "GoodLaser: blocked About dialog link:" << url;
            return;
        }
        QDesktopServices::openUrl(target);
    }
}  // namespace GL