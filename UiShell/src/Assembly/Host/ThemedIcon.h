#pragma once

#include <QColor>
#include <QGuiApplication>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QPalette>
#include <QSize>

namespace RD
{
    // 按当前调色板的文字色给图标着色
    inline QIcon themedIcon(const QString& key)
    {
        if (key.isEmpty())
            return QIcon();

        // 主题图标优先（Linux 桌面主题），取不到再按资源路径 / 文件路径加载
        QIcon base = QIcon::fromTheme(key, QIcon(key));
        if (base.isNull())
            return base;

        const QList<QSize> sizes = base.availableSizes(QIcon::Normal, QIcon::On);
        const QSize size = sizes.isEmpty() ? QSize(16, 16) : sizes.first();

        QPixmap pixmap = base.pixmap(size, QIcon::Normal, QIcon::On);
        if (pixmap.isNull())
            return base;

        const QColor textColor = QGuiApplication::palette().color(QPalette::Text);
        QPainter painter(&pixmap);
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(pixmap.rect(), textColor);
        painter.end();

        return QIcon(pixmap);
    }
}  // namespace RD
