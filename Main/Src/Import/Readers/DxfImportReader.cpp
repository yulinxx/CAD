/**
 * @file DxfImportReader.cpp
 * @brief DXF格式导入读取器实现
 */
#include "DxfImportReader.h"

DxfImportReader::DxfImportReader()
    : ImportReaderBase(Fio::FileFormat::DXF, { QStringLiteral("dxf") }, QStringLiteral("DXF"), ImportDimension::Dim2D)
{
}

ImportResult DxfImportReader::read(const ImportContext& context, Fio::VecAprEntityPtr& outEntities)
{
    // 唯一链路：中立 IR 导入（parseToIR → 单位换算 → FioEntityConverter），并收集图层表。
    // FileIO 不再直接实例化 Engine 对象，IR 为跨 DLL 安全的 POD。
    // 不再回退旧版 importFile：旧路径不还原图层/群组，其 IR 重试分支还漏掉了单位换算
    // （英制图纸会以 25.4 倍尺寸入场，见 Docs/09-框架审查/数据流审查.md I-2）。
    // IR 失败时如实报错，错误分类由 readViaIR 统一处理，与其余格式的 Reader 一致。
    return readViaIR(context, Fio::FileFormat::DXF, outEntities, true);
}