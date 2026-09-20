/**
 * @file StepExportWriter.cpp
 * @brief STEP 导出写入器实现
 */
#include "StepExportWriter.h"

StepExportWriter::StepExportWriter()
    : ExportWriterBase(Fio::FileFormat::STEP,
          { QStringLiteral("stp"), QStringLiteral("step") },
          QStringLiteral("STEP"),
          QStringLiteral("step"))
{
}