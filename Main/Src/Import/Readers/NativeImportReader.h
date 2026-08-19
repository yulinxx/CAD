#pragma once

#include "ImportReaderBase.h"

/// Native (.sy / .syx) 格式导入读取器
/// 支持 SanYi CAD 原生 2D 格式 (.sy) 和 3D 格式 (.syx)
class NativeImportReader : public ImportReaderBase
{
public:
    NativeImportReader();

    ImportResult read(const ImportContext& context, Fio::VecSyEntityPtr& outEntities) override;

protected:
    /// 成功消息：区分 2D/3D 原生格式
    QString successMessage(Fio::FileFormat format) const override;
};
