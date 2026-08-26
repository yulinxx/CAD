#pragma once

#include "ImportReaderBase.h"

/**
 * @brief OBJ 格式导入读取器
 * 走 FileIO 的 ObjParser + 中立 IR，由 Eg::FioEntityConverter 转成 SyMeshEntity，
 * 通过 SyEntity 基类指针返回。
 */
class ObjImportReader : public ImportReaderBase
{
public:
    ObjImportReader();

    ImportResult read(const ImportContext& context, Fio::VecSyEntityPtr& outEntities) override;
};
