#include "ObjImportReader.h"

#include "Log/SyLogger.h"

ImportResult ObjImportReader::read(const ImportContext& context,
    Fio::VecSyEntityPtr& outEntities)
{
    // OBJ 导入需要 3D 引擎的 ObjLoader，当前版本暂未直接集成
    // 后续可通过 Engine3D::ObjLoader 加载网格数据后转换为 SyEntity
    QString msg = QStringLiteral("OBJ import not yet implemented in ImportService. "
        "Please use Engine3D::ObjLoader directly for now.");
    SY_WARNF("[ObjImportReader] %s (path=%s)",
        msg.toUtf8().constData(),
        context.sourcePath.toUtf8().constData());

    return ImportResult::fail(msg, ImportErrorType::FormatNotSupported);
}