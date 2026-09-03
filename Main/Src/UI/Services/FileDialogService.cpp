#include "UI/Services/FileDialogService.h"

#include <QFileDialog>
#include <QWidget>

#include "FileIO/FileFormat.h"
#include "FileIO/FormatRegistry.h"
#include "Log/SyLogger.h"

// ==================== 对话框调用 ====================

QString FileDialogService::getOpenFileName(QWidget* parent, const QString& title, const QString& filter)
{
    SY_DEBUGF("[FileDialogService] getOpenFileName: title=%s, filter=%s",
        title.toUtf8().constData(),
        filter.toUtf8().constData());

    QString result = QFileDialog::getOpenFileName(parent, title, QString(), filter);

    SY_DEBUGF("[FileDialogService] getOpenFileName returned: %s", result.toUtf8().constData());

    return result;
}

QString FileDialogService::getSaveFileName(QWidget* parent, const QString& title, const QString& filter)
{
    return QFileDialog::getSaveFileName(parent, title, QString(), filter);
}

// ==================== 过滤器映射 ====================

QString FileDialogService::allSupportedFilter()
{
    return QObject::tr("All Supported Files (*.sy *.dxf *.plt *.stp *.step *.svg *.pdf *.ai *.obj *.stl);;"
                       "SanYi Files (*.sy);;DXF Files (*.dxf);;PLT Files (*.plt);;"
                       "STEP Files (*.stp *.step);;SVG Files (*.svg);;PDF Files (*.pdf);;"
                       "AI Files (*.ai);;OBJ Files (*.obj);;STL Files (*.stl);;All Files (*.*)");
}

QString FileDialogService::importFilterForFormat(Fio::FileFormat fmt)
{
    // 过滤器字符串统一来自 FormatRegistry（P1-11 收敛后的唯一入口）
    const char* filter = Fio::FormatRegistry::instance().importFilter(fmt);
    return filter ? QString::fromUtf8(filter) : QObject::tr("All Files (*.*)");
}

QString FileDialogService::exportFilterForFormat(Fio::FileFormat fmt)
{
    // 过滤器字符串统一来自 FormatRegistry（P1-11 收敛后的唯一入口）
    const char* filter = Fio::FormatRegistry::instance().exportFilter(fmt);
    return filter ? QString::fromUtf8(filter) : QObject::tr("All Files (*.*)");
}

QString FileDialogService::openFileFilter()
{
    // 2D 的“打开”只允许自定义 2D 文档（.sy），其它格式走“导入”
    return QObject::tr("SanYi 2D File (*.sy);;All Files (*.*)");
}

QString FileDialogService::saveFileFilter()
{
    return QObject::tr("SanYi Files (*.sy);;DXF Files (*.dxf);;SVG Files (*.svg);;All Files (*.*)");
}

QString FileDialogService::imageImportFilter()
{
    return QObject::tr("Image Files (*.png *.jpg *.jpeg *.bmp *.tga *.tiff *.gif *.webp);;All Files (*.*)");
}