#include "ImportReaderBase.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "FileIO/FileIOManager.h"
#include "Engine3D/Import/FioEntityConverter.h"
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
    const QByteArray tag = m_formatName.toUtf8();

    Fio::FileIOManager fileIO;
    std::string pathStr = context.sourcePath.toUtf8().toStdString();

    Fio::FioParseResult ir;
    char errBuf[1024] = { 0 };

    const auto startTime = std::chrono::steady_clock::now();
    const bool ok = fileIO.importToIR(pathStr.c_str(), format, &ir, errBuf, sizeof(errBuf));
    const auto parseMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startTime).count();

    // 解析失败与"解析成功但零实体"必须分开报：前者是文件/环境问题，后者多是内容为空或全部实体不受支持
    if (!ok)
    {
        SY_ERRORF("[ImportReader:%s] IR parse failed after %lld ms: %s",
            tag.constData(),
            static_cast<long long>(parseMs),
            errBuf[0] ? errBuf : "no error detail from FileIO");
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

    if (ir.entityCount == 0)
    {
        SY_WARNF("[ImportReader:%s] IR parse produced no entity after %lld ms: layers=%u, groups=%u, warnings=%u",
            tag.constData(),
            static_cast<long long>(parseMs),
            ir.layerCount,
            ir.groupCount,
            ir.warningCount);
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

    SY_INFOF("[ImportReader:%s] IR parsed in %lld ms: entities=%u, layers=%u, groups=%u, warnings=%u, unit='%s', "
             "sourceFormat='%s'",
        tag.constData(),
        static_cast<long long>(parseMs),
        ir.entityCount,
        ir.layerCount,
        ir.groupCount,
        ir.warningCount,
        ir.sourceUnit,
        ir.sourceFormat);

    // 转换图元的同时收集「图元 → 源图层 / 源群组 sourceId」映射，供构建文档阶段还原结构。
    // 群组映射无条件收集：群组不像图层那样只有部分格式支持，DXF 块引用、OBJ 的 o/g/usemtl
    // 都会产出群组，且收集成本只是一个哈希表。
    std::unordered_map<int64_t, uint32_t> entityLayerMap;
    std::unordered_map<int64_t, uint64_t> entityGroupMap;
    auto converted =
        Eg::FioEntityConverter::convertAll(ir, collectLayers ? &entityLayerMap : nullptr, &entityGroupMap);
    outEntities.clear();
    outEntities.reserve(converted.size());
    for (auto& e : converted)
    {
        outEntities.emplace_back(std::move(e));
    }

    // 转换层会跳过自己不认识的实体类型；数量对不上时必须点明差额，否则表现为"导入少了东西"却无迹可查
    if (outEntities.size() != static_cast<size_t>(ir.entityCount))
    {
        SY_WARNF("[ImportReader:%s] Converter dropped %zu of %u IR entity(ies): unsupported type or invalid geometry",
            tag.constData(),
            static_cast<size_t>(ir.entityCount) - outEntities.size(),
            ir.entityCount);
    }

    ImportResult res = ImportResult::ok(
        successMessage(format), static_cast<int>(outEntities.size()), static_cast<int>(ir.layerCount), QStringList{});

    // IR 只跨 DLL 传告警数量，不传文本（POD 契约）；文本留在 FileIO 侧日志里，
    // 这里把数量带给上层，便于 UI 提示"本次导入有告警，详见日志"
    if (ir.warningCount > 0)
    {
        res.addWarning(QStringLiteral("%1 parser reported %2 warning(s), see FileIO log for details")
                           .arg(m_formatName)
                           .arg(ir.warningCount));
    }

    if (collectLayers)
    {
        // 提取源文件图层表（名称/颜色/可见性），随导入结果带回
        res.importedLayers = Eg::FioEntityConverter::extractLayers(ir);
        res.entityLayerMap = std::move(entityLayerMap);
    }
    else if (ir.layerCount > 0)
    {
        SY_INFOF("[ImportReader:%s] IR carried %u layer(s) but layer collection is off for this format",
            tag.constData(),
            ir.layerCount);
    }

    if (ir.groupCount > 0)
    {
        // 提取源文件群组表（名称 + 父子关系），随导入结果带回，供 ImportService 重建 SyGroup
        res.importedGroups = Eg::FioEntityConverter::extractGroups(ir);
        res.entityGroupMap = std::move(entityGroupMap);
        SY_INFOF("[ImportReader:%s] IR carried %u group(s), %zu entity-group assignment(s)",
            tag.constData(),
            ir.groupCount,
            res.entityGroupMap.size());
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
    const QByteArray tag = m_formatName.toUtf8();

    ImportResult result;
    QString errMsg;
    if (tryImportViaIR(context, format, outEntities, collectLayers, &result, &errMsg))
    {
        SY_INFOF("[ImportReader:%s] read END: success, entities=%d, layers=%zu, layerMapped=%zu, groups=%zu",
            tag.constData(),
            result.entityCount,
            result.importedLayers.size(),
            result.entityLayerMap.size(),
            result.importedGroups.size());
        return result;
    }

    const ImportErrorType errorType = classifyError(errMsg);
    SY_ERRORF("[ImportReader:%s] read END: failed, errorType=%d, message=%s",
        tag.constData(),
        static_cast<int>(errorType),
        errMsg.toUtf8().constData());
    return ImportResult::fail(errMsg, errorType, QStringList{});
}

ImportResult ImportReaderBase::readViaLegacy(
    const ImportContext& context, Fio::FileFormat format, Fio::VecSyEntityPtr& outEntities) const
{
    const QByteArray tag = m_formatName.toUtf8();
    SY_WARNF("[ImportReader:%s] Falling back to the legacy import path (no IR, no layer/group restore)",
        tag.constData());

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

    const auto startTime = std::chrono::steady_clock::now();
    bool ok = fileIO.importFile(
        pathStr.c_str(), format, &raw, &count, errBuf, sizeof(errBuf), warningCb, &warns, &layerCount);
    const auto elapsedMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startTime).count();

    if (!ok)
    {
        QString msg = QString::fromUtf8(errBuf);
        decorateError(msg, context);
        SY_ERRORF("[ImportReader:%s] Legacy import failed after %lld ms: %s",
            tag.constData(),
            static_cast<long long>(elapsedMs),
            msg.toUtf8().constData());
        return ImportResult::fail(msg, classifyError(msg), warns);
    }

    outEntities.clear();
    outEntities.reserve(count);
    for (size_t i = 0; i < count; ++i)
    {
        outEntities.emplace_back(raw[i]);
    }
    Fio::FileIOManager::freeEntityArray(raw);

    for (const QString& warn : warns)
    {
        // Warnings are already collected in the result; log summary only
    }

    if (!warns.isEmpty())
    {
        SY_WARNF("[ImportReader:%s] Legacy parser produced %d warning(s)", tag.constData(), warns.size());
    }

    SY_INFOF("[ImportReader:%s] read END (legacy): success, entities=%zu, layers=%zu, %lld ms",
        tag.constData(),
        count,
        layerCount,
        static_cast<size_t>(warns.size()),
        static_cast<long long>(elapsedMs));

    return ImportResult::ok(successMessage(format), static_cast<int>(count), static_cast<int>(layerCount), warns);
}