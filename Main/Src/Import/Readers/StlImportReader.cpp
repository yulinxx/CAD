#include "StlImportReader.h"

#include "Log/SyLogger.h"

StlImportReader::StlImportReader()
    : ImportReaderBase(Fio::FileFormat::STL, { QStringLiteral("stl") }, QStringLiteral("STL"))
{
}

/**
 * @brief STL 文件导入实现
 * 统一走中立 IR 链路：FileIO 的 StlParser → parseToIR → Eg::FioEntityConverter。
 * StlParser 负责 ASCII / 二进制识别、三角形数量上限与法线兜底，本层不再重复实现。
 */
ImportResult StlImportReader::read(const ImportContext& context, Fio::VecSyEntityPtr& outEntities)
{
    SY_INFOF("[StlImportReader] read START: path=%s", context.sourcePath.toUtf8().constData());

    // StlParser 仅实现 IR 路径，无旧路径回退；STL 无图层概念，故不收集图层表
    return readViaIR(context, Fio::FileFormat::STL, outEntities, false);
}
