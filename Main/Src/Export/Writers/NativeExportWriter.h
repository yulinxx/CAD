#pragma once

#include "ExportWriterBase.h"

/// 原生 (.apr) 格式导出写入器 —— 用于 File_Save 的默认保存格式
class NativeExportWriter : public ExportWriterBase
{
public:
    NativeExportWriter();

    /// 覆写 write：借用实体（sceneManager 可用时）或复用已克隆图元，统一构造
    /// AprDocument 后交给 NativeWriter 落盘，并写入 ExportContext 里的加工语义快照
    /// （图层表 + 硬件 Profile 值集）。快照缺失时才在 ExportResult 上挂告警。
    ExportResult write(const ExportContext& context, const Fio::VecAprEntityPtr& entities) override;

protected:
    /// 按图元类型选择 3D/2D 原生格式（含网格图元时导出 .apr3d）
    Fio::FileFormat resolveFormat(const Fio::VecAprEntityPtr& entities) const override;

    QString successMessage() const override;
};
