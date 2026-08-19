#pragma once

#include "ExportWriterBase.h"

/// 原生 (.sy) 格式导出写入器 —— 用于 File_Save 的默认保存格式
class NativeExportWriter : public ExportWriterBase
{
public:
    NativeExportWriter();

protected:
    /// 按图元类型选择 3D/2D 原生格式（含网格图元时导出 .syx）
    Fio::FileFormat resolveFormat(const Fio::VecSyEntityPtr& entities) const override;

    QString successMessage() const override;
};
