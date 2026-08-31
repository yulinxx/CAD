#include "AiImportReader.h"

#include "Log/SyLogger.h"

AiImportReader::AiImportReader()
    : ImportReaderBase(Fio::FileFormat::AI, { QStringLiteral("ai") }, QStringLiteral("Adobe Illustrator"))
{
}

QString AiImportReader::successMessage(Fio::FileFormat /*format*/) const
{
    return QStringLiteral("AI import successful");
}

QString AiImportReader::noEntitiesMessage() const
{
    return QStringLiteral("AI import produced no entities");
}

void AiImportReader::decorateError(QString& msg, const ImportContext& /*context*/) const
{
    // 外部工具缺失（pdftocairo / Ghostscript）时给出更明确的错误
    if (msg.contains(QStringLiteral("pdftocairo"), Qt::CaseInsensitive) ||
        msg.contains(QStringLiteral("Ghostscript"), Qt::CaseInsensitive) ||
        msg.contains(QStringLiteral("not found"), Qt::CaseInsensitive))
    {
        msg = QStringLiteral("AI import requires external tools:\n%1").arg(msg);
    }
}

ImportResult AiImportReader::read(const ImportContext& context, Fio::VecSyEntityPtr& outEntities)
{

    // 走 FileIO 的 AiParser：内部将 PDF/PostScript 基 AI 转换为 SVG 后解析为中立 IR
    return readViaIR(context, Fio::FileFormat::AI, outEntities, true);
}