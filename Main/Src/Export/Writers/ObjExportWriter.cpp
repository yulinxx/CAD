#include "ObjExportWriter.h"

#include "Log/SyLogger.h"

ExportResult ObjExportWriter::write(const ExportContext& context,
    const Fio::VecSyEntityPtr& entities)
{
    // OBJ 导出需要 3D 引擎支持，当前版本暂未直接集成
    (void)entities;
    QString msg = QStringLiteral("OBJ export not yet implemented in ExportService. "
        "Please use Engine3D mesh export directly for now.");
    SY_WARNF("[ObjExportWriter] %s (path=%s)",
        msg.toUtf8().constData(),
        context.targetPath.toUtf8().constData());

    return ExportResult::fail(msg);
}