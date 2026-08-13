#pragma once

#include "../IImportReader.h"

/// STEP 格式导入读取器
class StepImportReader : public IImportReader
{
public:
    Fio::FileFormat format() const override
    {
        return Fio::FileFormat::STEP;
    }

    QStringList supportedExtensions() const override
    {
        return { QStringLiteral("stp"), QStringLiteral("step") };
    }

    QString formatName() const override
    {
        return QStringLiteral("STEP");
    }

    ImportResult read(const ImportContext& context, Fio::VecSyEntityPtr& outEntities) override;
};
