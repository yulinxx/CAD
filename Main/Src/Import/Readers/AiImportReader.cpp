#include "AiImportReader.h"

#include <unordered_map>

#include "FileIO/FileIOManager.h"
#include "Import/FioEntityConverter.h"
#include "Log/SyLogger.h"
#include "Engine/SyEntity/SyEntity.h"

ImportResult AiImportReader::read(const ImportContext& context, Fio::VecSyEntityPtr& outEntities)
{
    SY_INFOF("[AiImportReader] read START: path=%s", context.sourcePath.toUtf8().constData());

    Fio::FileIOManager fileIO;
    const std::string pathStr = context.sourcePath.toUtf8().toStdString();

    // 走 FileIO 的 AiParser：内部将 PDF/PostScript 基 AI 转换为 SVG 后解析为中立 IR
    Fio::FioParseResult ir;
    char errBuf[1024] = { 0 };
    const bool ok = fileIO.importToIR(pathStr.c_str(), Fio::FileFormat::AI, &ir, errBuf, sizeof(errBuf));

    if (!ok || ir.entityCount == 0)
    {
        QString msg = QString::fromUtf8(errBuf);
        if (msg.isEmpty())
        {
            msg = QStringLiteral("AI import produced no entities");
        }
        SY_ERRORF("[AiImportReader] Failed: %s", msg.toUtf8().constData());

        // 外部工具缺失（pdftocairo / Ghostscript）时给出更明确的错误
        if (msg.contains(QStringLiteral("pdftocairo"), Qt::CaseInsensitive) ||
            msg.contains(QStringLiteral("Ghostscript"), Qt::CaseInsensitive) ||
            msg.contains(QStringLiteral("not found"), Qt::CaseInsensitive))
        {
            msg = QStringLiteral("AI import requires external tools:\n%1").arg(msg);
        }

        ImportErrorType errorType = ImportErrorType::ParseFailed;
        if (msg.contains(QStringLiteral("file not found"), Qt::CaseInsensitive) ||
            msg.contains(QStringLiteral("cannot open"), Qt::CaseInsensitive))
        {
            errorType = ImportErrorType::FileNotFound;
        }
        return ImportResult::fail(msg, errorType, QStringList{});
    }

    // 转换图元的同时收集「图元 → 源图层 sourceId」映射，供构建文档阶段还原图层结构
    std::unordered_map<int64_t, uint32_t> entityLayerMap;
    auto converted = FioEntityConverter::convertAll(ir, &entityLayerMap);
    outEntities.clear();
    outEntities.reserve(converted.size());
    for (auto& e : converted)
    {
        outEntities.emplace_back(std::move(e));
    }

    // 提取源文件图层表（名称/颜色/可见性），随导入结果带回
    std::vector<Fio::IrLayerInfo> importedLayers = FioEntityConverter::extractLayers(ir);

    SY_INFOF("[AiImportReader] read END: success, entities=%zu, layers=%zu, mapped=%zu",
        outEntities.size(),
        importedLayers.size(),
        entityLayerMap.size());

    ImportResult result = ImportResult::ok(QStringLiteral("AI import successful"),
        static_cast<int>(outEntities.size()),
        static_cast<int>(importedLayers.size()),
        QStringList{});
    result.importedLayers = std::move(importedLayers);
    result.entityLayerMap = std::move(entityLayerMap);
    return result;
}
