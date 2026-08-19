#include "DxfImportReader.h"

#include "Log/SyLogger.h"

DxfImportReader::DxfImportReader()
    : ImportReaderBase(Fio::FileFormat::DXF, { QStringLiteral("dxf") }, QStringLiteral("DXF"))
{
}

ImportResult DxfImportReader::read(const ImportContext& context, Fio::VecSyEntityPtr& outEntities)
{
    SY_INFOF("[DxfImportReader] read START: path=%s", context.sourcePath.toUtf8().constData());

    // 主链路：中立 IR 导入（parseToIR → FioEntityConverter）
    // FileIO 不再直接实例化 Engine 对象，IR 为跨 DLL 安全的 POD；
    // 若 IR 路径失败（如解析器未实现 IR 或文件不支持），自动回退旧路径。
    ImportResult result;
    QString errMsg;
    if (tryImportViaIR(context, Fio::FileFormat::DXF, outEntities, true, &result, &errMsg))
    {
        return result;
    }
    SY_WARNF("[DxfImportReader] IR path unavailable, falling back to legacy: %s",
        errMsg.isEmpty() ? "no entities" : errMsg.toUtf8().constData());

    // 回退路径：旧版 importFile（不携带图层表信息，图层结构无法还原，图元将归入当前图层）
    return readViaLegacy(context, Fio::FileFormat::DXF, outEntities);
}
