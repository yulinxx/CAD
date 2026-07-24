#include "UI/FileDialogService.h"

#include <QFileDialog>
#include <QWidget>

#include "FileIO/FileFormat.h"

// ==================== 对话框调用 ====================

QString FileDialogService::getOpenFileName(QWidget* parent, const QString& title, const QString& filter)
{
    return QFileDialog::getOpenFileName(parent, title, QString(), filter);
}

QString FileDialogService::getSaveFileName(QWidget* parent, const QString& title, const QString& filter)
{
    return QFileDialog::getSaveFileName(parent, title, QString(), filter);
}

// ==================== 过滤器映射 ====================

QString FileDialogService::allSupportedFilter()
{
    return QObject::tr("All Supported Files (*.sy *.dxf *.plt *.stp *.step *.svg *.pdf *.ai);;"
        "SanYi Files (*.sy);;DXF Files (*.dxf);;PLT Files (*.plt);;"
        "STEP Files (*.stp *.step);;SVG Files (*.svg);;PDF Files (*.pdf);;"
        "AI Files (*.ai);;All Files (*.*)");
}

QString FileDialogService::importFilterForFormat(Fio::FileFormat fmt)
{
    switch (fmt)
    {
        case Fio::FileFormat::DXF:  return QObject::tr("DXF Files (*.dxf);;All Files (*.*)");
        case Fio::FileFormat::SVG:  return QObject::tr("SVG Files (*.svg);;All Files (*.*)");
        case Fio::FileFormat::PLT:  return QObject::tr("PLT Files (*.plt *.hpgl);;All Files (*.*)");
        case Fio::FileFormat::STEP: return QObject::tr("STEP Files (*.stp *.step);;All Files (*.*)");
        case Fio::FileFormat::PDF:  return QObject::tr("PDF Files (*.pdf);;All Files (*.*)");
        default:                   return QObject::tr("All Files (*.*)");
    }
}

QString FileDialogService::exportFilterForFormat(Fio::FileFormat fmt)
{
    switch (fmt)
    {
        case Fio::FileFormat::DXF:  return QObject::tr("DXF Files (*.dxf);;All Files (*.*)");
        case Fio::FileFormat::SVG:  return QObject::tr("SVG Files (*.svg);;All Files (*.*)");
        case Fio::FileFormat::PLT:  return QObject::tr("PLT Files (*.plt);;All Files (*.*)");
        case Fio::FileFormat::BMP:  return QObject::tr("BMP Files (*.bmp);;All Files (*.*)");
        case Fio::FileFormat::PNG:  return QObject::tr("PNG Files (*.png);;All Files (*.*)");
        default:                   return QObject::tr("All Files (*.*)");
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