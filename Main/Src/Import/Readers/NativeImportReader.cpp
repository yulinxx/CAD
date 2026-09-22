/**
 * @file NativeImportReader.cpp
 * @brief 原生格式导入读取器实现
 */
#include "NativeImportReader.h"

#include <QFileInfo>

#include "FileIO/Parsers/NativeParser.h"
#include "FileIO/SyDocument.h"

#include "Log/SyLogger.h"

NativeImportReader::NativeImportReader()
    : ImportReaderBase(Fio::FileFormat::Native,
          { QStringLiteral("sy"), QStringLiteral("syx") },
          QStringLiteral("SanYi Native"),
          ImportDimension::Both)
{
}

ImportDimension NativeImportReader::dimension() const
{
    // NativeImportReader 同时支持 .sy (2D) 和 .syx (3D)
    // 由于无法在构造时预知会读取哪个文件，这里返回 Both
    // 实际过滤由调用方在读取时根据文件后缀判断
    return ImportDimension::Both;
}

ImportResult NativeImportReader::read(const ImportContext& context, Fio::VecSyEntityPtr& outEntities)
{
    // 检测是否为 3D 格式 (.syx)
    QFileInfo fi(context.sourcePath);
    bool is3D = (fi.suffix().toLower() == QStringLiteral("syx"));
    Fio::FileFormat format = is3D ? Fio::FileFormat::Native3D : Fio::FileFormat::Native;

    SY_DEBUGF("[NativeImportReader] Importing native format: %s (format=%d)",
        is3D ? "3D (.syx)" : "2D (.sy)",
        static_cast<int>(format));

    // 使用新管线：parseDocument 获取完整 SyDocument，通过公开 API 提取图层/群组信息
    Fio::NativeParser parser(format);
    Fio::SyDocument doc;
    auto parseResult = parser.parseDocument(context.sourcePath.toUtf8().constData(), doc);

    if (!parseResult.success)
    {
        SY_ERRORF("[NativeImportReader] parseDocument failed: %s", parseResult.errorMessage.c_str());
        return ImportResult::fail(QString::fromStdString(parseResult.errorMessage), ImportErrorType::ParseFailed, QStringList{});
    }

    // 准备 ImportResult
    ImportResult result = ImportResult::ok(successMessage(format), static_cast<int>(doc.entityCount()), static_cast<int>(doc.layerCount()), {});

    // 1. 提取图层
    result.importedLayers.reserve(doc.layerCount());
    for (size_t i = 0; i < doc.layerCount(); ++i)
    {
        Fio::SyLayerInfo layerInfo;
        if (doc.getLayerAt(i, layerInfo))
        {
            Fio::IrLayerInfo irLayer;
            irLayer.sourceId = layerInfo.id;
            std::strncpy(irLayer.name, layerInfo.name, sizeof(irLayer.name) - 1);
            irLayer.color = layerInfo.color;
            irLayer.visible = layerInfo.visible;
            irLayer.locked = layerInfo.locked;
            result.importedLayers.push_back(irLayer);
        }
    }

    // 2. 提取群组
    result.importedGroups.reserve(doc.groupCount());
    for (size_t i = 0; i < doc.groupCount(); ++i)
    {
        Fio::SyGroupInfo groupInfo;
        if (doc.getGroupAt(i, groupInfo))
        {
            Fio::IrGroupInfo irGroup;
            irGroup.sourceId = groupInfo.id;
            irGroup.parentSourceId = groupInfo.parentGroupId;
            std::strncpy(irGroup.name, groupInfo.name, sizeof(irGroup.name) - 1);
            result.importedGroups.push_back(irGroup);
        }
    }

    // 3. 建立图元 -> 图层映射
    result.entityLayerMap.reserve(doc.entityCount());
    for (size_t i = 0; i < doc.entityCount(); ++i)
    {
        Eg::SyEntity* entity = doc.entityAt(i);
        if (entity)
        {
            uint32_t layerId = doc.entityLayerId(entity->id);
            if (layerId != 0)
            {
                result.entityLayerMap[static_cast<int64_t>(entity->id)] = layerId;
            }
        }
    }

    // 4. 建立图元 -> 群组映射
    for (size_t i = 0; i < doc.entityCount(); ++i)
    {
        Eg::SyEntity* entity = doc.entityAt(i);
        if (entity)
        {
            uint64_t groupId = doc.entityGroupId(entity->id);
            if (groupId != 0)
            {
                result.entityGroupMap[static_cast<int64_t>(entity->id)] = groupId;
            }
        }
    }

    // 5. 移动图元到输出
    outEntities.clear();
    outEntities.reserve(doc.entityCount());
    for (size_t i = 0; i < doc.entityCount(); ++i)
    {
        // 这里无法直接移动，因为 entityAt 返回借用指针
        // 需要通过反序列化时已经存入文档的 unique_ptr
        // 但 SyDocument 公开 API 不支持移动所有权
        // 因此这里使用 clone
        Eg::SyEntity* entity = doc.entityAt(i);
        if (entity)
        {
            outEntities.emplace_back(entity->clone());
        }
    }

    // 复制警告
    for (const auto& warn : parseResult.warnings)
    {
        result.addWarning(QString::fromStdString(warn));
    }

    SY_DEBUGF("[NativeImportReader] Imported via new pipeline: entities=%zu, layers=%zu, groups=%zu, layerMap=%zu, groupMap=%zu",
        outEntities.size(),
        result.importedLayers.size(),
        result.importedGroups.size(),
        result.entityLayerMap.size(),
        result.entityGroupMap.size());

    return result;
}

QString NativeImportReader::successMessage(Fio::FileFormat format) const
{
    return format == Fio::FileFormat::Native3D ? QStringLiteral("SanYi 3D Native import successful")
                                               : QStringLiteral("SanYi 2D Native import successful");
}