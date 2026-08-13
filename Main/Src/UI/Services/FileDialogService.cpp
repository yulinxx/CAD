#include "UI/Services/FileDialogService.h"

#include <QFileDialog>
#include <QWidget>

#include "FileIO/FileFormat.h"
#include "Log/SyLogger.h"

// ==================== 对话框调用 ====================

QString FileDialogService::getOpenFileName(QWidget* parent, const QString& title, const QString& filter)
{
    SY_INFOF("[FileDialogService] getOpenFileName: title=%s, filter=%s",
        title.toUtf8().constData(),
        filter.toUtf8().constData());

    QString result = QFileDialog::getOpenFileName(parent, title, QString(), filter);

    SY_INFOF("[FileDialogService] getOpenFileName returned: %s", result.toUtf8().constData());

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
    switch (fmt)
    {
    case Fio::FileFormat::DXF:
        return QObject::tr("DXF Files (*.dxf);;All Files (*.*)");
    case Fio::FileFormat::SVG:
        return QObject::tr("SVG Files (*.svg);;All Files (*.*)");
    case Fio::FileFormat::PLT:
        return QObject::tr("PLT Files (*.plt *.hpgl);;All Files (*.*)");
    case Fio::FileFormat::STEP:
        return QObject::tr("STEP Files (*.stp *.step);;All Files (*.*)");
    case Fio::FileFormat::PDF:
        return QObject::tr("PDF Files (*.pdf);;All Files (*.*)");
    case Fio::FileFormat::OBJ:
        return QObject::tr("OBJ Files (*.obj);;All Files (*.*)");
    case Fio::FileFormat::STL:
        return QObject::tr("STL Files (*.stl);;All Files (*.*)");
    default:
        return QObject::tr("All Files (*.*)");
    }
}

QString FileDialogService::exportFilterForFormat(Fio::FileFormat fmt)
{
    switch (fmt)
    {
    case Fio::FileFormat::DXF:
        return QObject::tr("DXF Files (*.dxf);;All Files (*.*)");
    case Fio::FileFormat::SVG:
        return QObject::tr("SVG Files (*.svg);;All Files (*.*)");
    case Fio::FileFormat::PLT:
        return QObject::tr("PLT Files (*.plt);;All Files (*.*)");
    case Fio::FileFormat::BMP:
        return QObject::tr("BMP Files (*.bmp);;All Files (*.*)");
    case Fio::FileFormat::PNG:
        return QObject::tr("PNG Files (*.png);;All Files (*.*)");
    default:
        return QObject::tr("All Files (*.*)");
    }
}

QString FileDialogService::openFileFilter()
{
    return allSupportedFilter();
}

QString FileDialogService::saveFileFilter()
{
    return QObject::tr("SanYi Files (*.sy);;DXF Files (*.dxf);;SVG Files (*.svg);;All Files (*.*)");
}

QString FileDialogService::imageImportFilter()
{
    return QObject::tr("Image Files (*.png *.jpg *.jpeg *.bmp *.tga *.tiff *.gif *.webp);;All Files (*.*)");
}