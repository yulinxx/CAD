#pragma once

#include <QDialog>
#include <QString>
#include <QVariantMap>

class QQuickWidget;

namespace GL
{
    // 对话框
    class AboutDialog : public QDialog
    {
        Q_OBJECT

    public:
        explicit AboutDialog(const QVariantMap& view, QWidget* parent = nullptr);

        // QML 是否加载成功
        bool isLoaded() const;

        // 加载失败的原因
        QString loadError() const;

    private slots:
        void openLink(const QString& url);

    private:
        QQuickWidget* m_quick = nullptr;
        QString m_loadError;
    };
}  // namespace GL
