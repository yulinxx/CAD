#include "ObjImportReader.h"

#include "Log/SyLogger.h"

ObjImportReader::ObjImportReader()
    : ImportReaderBase(Fio::FileFormat::OBJ, { QStringLiteral("obj") }, QStringLiteral("OBJ"))
{
}

/**
 * @brief OBJ 文件导入实现
 * 统一走中立 IR 链路：FileIO 的 ObjParser → parseToIR → Eg::FioEntityConverter，
 * 与 2D 格式（DXF / SVG / PLT）保持同一条路径。OBJ 的 o / g / usemtl 分段
 * 由 ObjParser 拆成多个 Mesh3D 实体并带出群组信息，因此这里不再合并为单一网格。
 */
ImportResult ObjImportReader::read(const ImportContext& context, Fio::VecSyEntityPtr& outEntities)
{

    // ObjParser 仅实现 IR 路径，无旧路径回退；OBJ 无图层概念，故不收集图层表
    return readViaIR(context, Fio::FileFormat::OBJ, outEntities, false);
}
