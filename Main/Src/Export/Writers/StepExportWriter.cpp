#include "StepExportWriter.h"

StepExportWriter::StepExportWriter()
    : ExportWriterBase(Fio::FileFormat::STEP,
          { QStringLiteral("stp"), QStringLiteral("step") },
          QStringLiteral("STEP"),
          QStringLiteral("step"))
{
}