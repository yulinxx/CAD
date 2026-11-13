// 导入能力判据一致性用例
// 目的：锁定「拖拽与菜单共用同一份扩展名/维度判据」，防止两侧各自维护列表再次漂移

#include <gtest/gtest.h>

#include <memory>

#include "Import/ImportDispatcher.h"
#include "Import/ImportService.h"
#include "Import/Readers/AiImportReader.h"
#include "Import/Readers/DxfImportReader.h"
#include "Import/Readers/NativeImportReader.h"
#include "Import/Readers/ObjImportReader.h"
#include "Import/Readers/PdfImportReader.h"
#include "Import/Readers/PltImportReader.h"
#include "Import/Readers/StepImportReader.h"
#include "Import/Readers/StlImportReader.h"
#include "Import/Readers/SvgImportReader.h"
#include "Import/Readers/UgImportReader.h"
#include "UI/Services/FileDialogService.h"
#include "UI/Workbench/FileDropHandler.h"

// 拖拽与菜单必须共用同一份判据：扩展名只在 ImportService 登记，两侧同时生效
TEST(ImportExportRegressionTest, ImportCapability_DragAndMenuShareSingleSource)
{
    // 与产品装配一致地注册全部读取器
    ImportDispatcher dispatcher;
    dispatcher.registerReader(std::make_unique<DxfImportReader>());
    dispatcher.registerReader(std::make_unique<SvgImportReader>());
    dispatcher.registerReader(std::make_unique<PdfImportReader>());
    dispatcher.registerReader(std::make_unique<StepImportReader>());
    dispatcher.registerReader(std::make_unique<ObjImportReader>());
    dispatcher.registerReader(std::make_unique<StlImportReader>());
    dispatcher.registerReader(std::make_unique<PltImportReader>());
    dispatcher.registerReader(std::make_unique<AiImportReader>());
    dispatcher.registerReader(std::make_unique<NativeImportReader>());
    dispatcher.registerReader(std::make_unique<UgImportReader>());

    ImportService service;
    service.setDispatcher(&dispatcher);

    FileDropHandler dropHandler;
    dropHandler.setImportService(&service);

    for (const QString& workbenchId : { QStringLiteral("2D"), QStringLiteral("3D") })
    {
        dropHandler.setCurrentWorkbenchId(workbenchId);
        const QStringList dragExtensions = dropHandler.supportedExtensions();
        const QStringList menuExtensions = service.importableExtensions(workbenchId);

        // 拖拽与菜单的判据必须完全一致，任何一侧单独加列表都会在这里失败
        EXPECT_EQ(dragExtensions, menuExtensions)
            << "drag/menu capability diverged for workbench " << workbenchId.toStdString();
        EXPECT_TRUE(dragExtensions.contains(QStringLiteral("tif")));

        // 菜单过滤器必须覆盖同一份判据，不能出现「拖拽能导入、菜单选不到」
        const QString filter = FileDialogService::allSupportedFilter(menuExtensions);
        for (const QString& ext : menuExtensions)
        {
            EXPECT_TRUE(filter.contains(QStringLiteral("*.") + ext)) << ext.toStdString();
        }
    }

    // 维度判据必须传给菜单侧：2D 不含 3D 格式，3D 不含 2D 格式
    const QStringList exts2D = service.importableExtensions(QStringLiteral("2D"));
    const QStringList exts3D = service.importableExtensions(QStringLiteral("3D"));
    for (const QString& ext : { QStringLiteral("dxf"), QStringLiteral("svg"), QStringLiteral("plt"),
             QStringLiteral("hpgl"), QStringLiteral("svgz"), QStringLiteral("pdf"), QStringLiteral("ai"),
             QStringLiteral("igs"), QStringLiteral("iges"), QStringLiteral("apr"), QStringLiteral("apr3d") })
    {
        EXPECT_TRUE(exts2D.contains(ext)) << ext.toStdString();
    }
    for (const QString& ext : { QStringLiteral("stp"), QStringLiteral("step"), QStringLiteral("obj"),
             QStringLiteral("stl"), QStringLiteral("apr"), QStringLiteral("apr3d") })
    {
        EXPECT_TRUE(exts3D.contains(ext)) << ext.toStdString();
    }
    EXPECT_FALSE(exts2D.contains(QStringLiteral("stl")));
    EXPECT_FALSE(exts3D.contains(QStringLiteral("dxf")));

    // 图片对话框与拖拽共用同一份位图判据
    const QString imageFilter = FileDialogService::imageImportFilter();
    for (const QString& ext : ImportService::imageExtensions())
    {
        EXPECT_TRUE(imageFilter.contains(QStringLiteral("*.") + ext)) << ext.toStdString();
        EXPECT_TRUE(ImportService::isImageExtension(ext)) << ext.toStdString();
    }

    // 单格式菜单的扩展名取读取器判据，不再包含 FormatRegistry 登记但无解析器的 prt
    const QStringList ugExtensions = service.extensionsForFormat(Fio::FileFormat::UG);
    EXPECT_TRUE(ugExtensions.contains(QStringLiteral("igs")));
    EXPECT_TRUE(ugExtensions.contains(QStringLiteral("iges")));
    EXPECT_FALSE(ugExtensions.contains(QStringLiteral("prt")));
}
