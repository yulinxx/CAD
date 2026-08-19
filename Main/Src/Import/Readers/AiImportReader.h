#pragma once

#include "ImportReaderBase.h"

/// Adobe Illustrator (AI) 格式导入读取器
///
/// 现代 AI 文件（AI 8+）本质是 PDF，旧版（AI 7-）为 PostScript。
/// FileIO 的 AiParser 会调用 pdftocairo/Ghostscript 将其转换为 SVG，
/// 再经 SvgParser 解析为中立 IR。本读取器仅负责触发该链路。
class AiImportReader : public ImportReaderBase
{
public:
    AiImportReader();
    ImportResult read(const ImportContext& context, Fio::VecSyEntityPtr& outEntities) override;

protected:
    QString successMessage(Fio::FileFormat format) const override;
    QString noEntitiesMessage() const override;
    void decorateError(QString& msg, const ImportContext& context) const override;
};
