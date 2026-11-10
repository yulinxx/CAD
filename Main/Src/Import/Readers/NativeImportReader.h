#pragma once

#include "ImportReaderBase.h"

/// Native (.apr / .apr3d) 格式导入读取器
/// 支持 April CAD 原生 2D 格式 (.apr) 和 3D 格式 (.apr3d)
class NativeImportReader : public ImportReaderBase
{
public:
    NativeImportReader();

    ImportResult read(const ImportContext& context, Fio::VecAprEntityPtr& outEntities) override;

    /// 重写 dimension()，因为 .apr 是 2D，.apr3d 是 3D，需要在读取时动态判断
    ImportDimension dimension() const override;

protected:
    /// 成功消息：区分 2D/3D 原生格式
    QString successMessage(Fio::FileFormat format) const override;
};
