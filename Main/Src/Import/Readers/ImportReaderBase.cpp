#include "ImportReaderBase.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "FileIO/FileIOManager.h"
#include "Import/FioEntityConverter.h"
#include "Log/SyLogger.h"
#include "Engine/SyEntity/SyEntity.h"

ImportReaderBase::ImportReaderBase(Fio::FileFormat format, QStringList extensions, QString formatName)
    : m_format(format)
    , m_extensions(std::move(extensions))
    , m_formatName(std::move(formatName))
{
}

Fio::FileFormat ImportReaderBase::format() const
{
    return m_format;
}

QStringList ImportReaderBase::supportedExtensions() const
{
    return m_extensions;
}

QString ImportReaderBase::formatName() const
{
    return m_formatName;
}

ImportErrorType ImportReaderBase::classifyError(const QString& msg)
{
    if (msg.contains(QStringLiteral("file not found"), Qt::CaseInsensitive) ||
        msg.contains(QStringLiteral("cannot open"), Qt::CaseInsensitive))
    {
        return ImportErrorType::FileNotFound;
    }
    if (msg.contains(QStringLiteral("unit"), Qt::CaseInsensitive) ||
        msg.contains(QStringLiteral("scale"), Qt::CaseInsensitive))
    {
        return ImportErrorType::UnitIncompatible;
    }
    if (msg.contains(QStringLiteral("coordinate"), Qt::CaseInsensitive) ||
        msg.contains(QStringLiteral("axis"), Qt::CaseInsensitive))
    {
        return ImportErrorType::CoordinateSystemIncompatible;
    }
    return ImportErrorType::ParseFailed;
}

QString ImportReaderBase::successMessage(Fio::FileFormat /*format*/) const
{
    return QStringLiteral("%1 import successful").arg(m_formatName);
}

QString ImportReaderBase::noEntitiesMessage() const
{
    return QStringLiteral("%1 import produced no entities").arg(m_formatName);
}

void ImportReaderBase::decorateError(QString& /*msg*/, const ImportContext& /*context*/) const
{
    // 默认不增强错误提示，子类按需覆写
}

bool ImportReaderBase::tryImportViaIR(const ImportContext& context,
    Fio::FileFormat format,
    Fio::VecSyEntityPtr& outEntities,
    bool collectLayers,
    ImportResult* result,
    QString* errMsg) const
{
    Fio::FileIOManager fileIO;
    std::string pathStr = context.sourcePath.toUtf8().toStdString();

    Fio::FioParseResult ir;
    char errBuf[1024] = { 0 };
    bool ok = fileIO.importToIR(pathStr.c_str(), format, &ir, errBuf, sizeof(errBuf));

    if (!ok || ir.entityCount == 0)
    {
        if (errMsg)
        {
            *errMsg = QString::fromUtf8(errBuf);
            if (errMsg->isEmpty())
            {
                *errMsg = noEntitiesMessage();
            }
            decorateError(*errMsg, context);
        }
        return false;
    }

    // 转换图元的同时收集「图元 → 源图层 sourceId」映射，供构建文档阶段还原图层结构
    std::unordered_map<int64_t, uint32_t> entityLayerMap;
    auto converted = FioEntityConverter::convertAll(ir, collectLayers ? &entityLayerMap : nullptr);
    outEntities.clear();
    outEntities.reserve(converted.size());
    for (auto& e : converted)
    {
        outEntities.emplace_back(std::move(e));
    }

    ImportResult res = ImportResult::ok(
        successMessage(format), static_cast<int>(outEntities.size()), static_cast<int>(ir.layerCount), QStringList{});

    if (collectLayers)
    {
        // 提取源文件图层表（名称/颜色/可见性），随导入结果带回
        res.importedLayers = FioEntityConverter::extractLayers(ir);
        res.entityLayerMap = std::move(entityLayerMap);
    }

    if (result)
    {
        *result = std::move(res);
    }
    return true;
}

ImportResult ImportReaderBase::readViaIR(
    const ImportContext& context, Fio::FileFormat format, Fio::VecSyEntityPtr& outEntities, bool collectLayers) const
{
    ImportResult result;
    QString errMsg;
    if (tryImportViaIR(context, format, outEntities, collectLayers, &result, &errMsg))
    {
        SY_INFOF("[%s] read END: success, entities=%d, layers=%u, mapped=%zu",
            m_formatName.toUtf8().constData(),
            result.entityCount,
            result.importedLayers.size(),
            result.entityLayerMap.size());
        return result;
    }

    SY_ERRORF("[%s] Failed: %s", m_formatName.toUtf8().constData(), errMsg.toUtf8().constData());
    return ImportResult::fail(errMsg, classifyError(errMsg), QStringList{});
}

ImportResult ImportReaderBase::readViaLegacy(
    const ImportContext& context, Fio::FileFormat format, Fio::VecSyEntityPtr& outEntities) const
{
    Fio::FileIOManager fileIO;
    std::string pathStr = context.sourcePath.toUtf8().toStdString();

    QStringList warns;
    Fio::FileIOManager::WarningCallback warningCb = [](const char* warning, void* ctx) {
        static_cast<QStringList*>(ctx)->append(QString::fromUtf8(warning));
    };

    Eg::SyEntity** raw = nullptr;
    size_t count = 0;
    size_t layerCount = 0;
    char errBuf[1024] = { 0 };

    bool ok = fileIO.importFile(
        pathStr.c_str(), format, &raw, &count, errBuf, sizeof(errBuf), warningCb, &warns, &layerCount);

    if (!ok)
    {
        QString msg = QString::fromUtf8(errBuf);
        decorateError(msg, context);
        SY_ERRORF("[%s] Failed: %s", m_formatName.toUtf8().constData(), msg.toUtf8().constData());
        return ImportResult::fail(msg, classifyError(msg), warns);
    }

    outEntities.clear();
    outEntities.reserve(count);
    for (size_t i = 0; i < count; ++i)
    {
        outEntities.emplace_back(raw[i]);
    }
    Fio::FileIOManager::freeEntityArray(raw);

    SY_INFOF("[%s] read END: success, entities=%zu, layers=%zu", m_formatName.toUtf8().constData(), count, layerCount);

    return ImportResult::ok(successMessage(format), static_cast<int>(count), static_cast<int>(layerCount), warns);
}