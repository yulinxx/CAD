/**
 * @file FileDialogService.cpp
 * @brief 文件对话框服务实现
 *
 * 提供打开/保存文件对话框的封装。
 */
#include "UI/Services/FileDialogService.h"

#include <QFileDialog>
#include <QWidget>

#include "FileIO/FileFormat.h"
#include "FileIO/FormatRegistry.h"
#include "Import/ImportService.h"
#include "Log/AprLogger.h"

// ==================== 对话框调用 ====================

QString FileDialogService::getOpenFileName(QWidget* parent, const QString& title, const QString& filter)
{
    APR_DEBUGF("[FileDialogService] getOpenFileName: title=%s, filter=%s",
        title.toUtf8().constData(),
        filter.toUtf8().constData());

    QString result = QFileDialog::getOpenFileName(parent, title, QString(), filter);

    APR_DEBUGF("[FileDialogService] getOpenFileName returned: %s", result.toUtf8().constData());

    return result;
}

QString FileDialogService::getSaveFileName(QWidget* parent, const QString& title, const QString& filter)
{
    return QFileDialog::getSaveFileName(parent, title, QString(), filter);
}

// ==================== 过滤器映射 ====================

QString FileDialogService::importFilter(const QString& label, const QStringList& extensions)
{
    if (extensions.isEmpty())
    {
        return QObject::tr("All Files (*.*)");
    }

    QString wildcards;
    for (const QString& ext : extensions)
    {
        if (!wildcards.isEmpty())
        {
            wildcards += QLatin1Char(' ');
        }
        wildcards += QStringLiteral("*.") + ext;
    }
    return QStringLiteral("%1 (%2);;All Files (*.*)").arg(label, wildcards);
}

QString FileDialogService::allSupportedFilter(const QStringList& extensions)
{
    return importFilter(QObject::tr("All Supported Files"), extensions);
}

QString FileDialogService::importFilterForFormat(Fio::FileFormat fmt, const QStringList& extensions)
{
    if (extensions.isEmpty())
    {
        // 读取器未登记扩展名时退回 FormatRegistry，避免对话框失去过滤能力
        const char* filter = Fio::FormatRegistry::instance().importFilter(fmt);
        return filter ? QString::fromUtf8(filter) : QObject::tr("All Files (*.*)");
    }

    // 扩展名以读取器判据为准；格式名仍复用 FormatRegistry，避免两处维护文案
    QString label = QObject::tr("Files");
    if (const char* registryFilter = Fio::FormatRegistry::instance().importFilter(fmt))
    {
        const QString full = QString::fromUtf8(registryFilter);
        const int suffixPos = full.indexOf(QStringLiteral(" ("));
        label = suffixPos > 0 ? full.left(suffixPos) : full;
    }
    return importFilter(label, extensions);
}

QString FileDialogService::exportFilterForFormat(Fio::FileFormat fmt)
{
    // 过滤器字符串统一来自 FormatRegistry
    const char* filter = Fio::FormatRegistry::instance().exportFilter(fmt);
    return filter ? QString::fromUtf8(filter) : QObject::tr("All Files (*.*)");
}

QString FileDialogService::openFileFilter()
{
    // 2D 的“打开”只允许自定义 2D 文档（.apr），其它格式走“导入”
    return QObject::tr("April 2D File (*.apr);;All Files (*.*)");
}

QString FileDialogService::saveFileFilter()
{
    return QObject::tr("April Files (*.apr);;DXF Files (*.dxf);;SVG Files (*.svg);;All Files (*.*)");
}

QString FileDialogService::imageImportFilter()
{
    // 位图判据唯一来源：与拖拽共用 ImportService::imageExtensions()
    return importFilter(QObject::tr("Image Files"), ImportService::imageExtensions());
}