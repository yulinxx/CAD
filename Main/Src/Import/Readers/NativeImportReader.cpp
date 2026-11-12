/**
 * @file NativeImportReader.cpp
 * @brief 原生格式导入读取器实现
 */
#include "NativeImportReader.h"

#include <QFileInfo>

#include <unordered_set>
#include <vector>

#include "FileIO/Parsers/NativeParser.h"
#include "FileIO/AprDocument.h"
#include "FileIO/ProcessModelSnapshot.h"
#include "Engine/AprEntity/AprEntity.h"

#include "Log/AprLogger.h"

NativeImportReader::NativeImportReader()
    : ImportReaderBase(Fio::FileFormat::Native,
          { QStringLiteral("apr"), QStringLiteral("apr3d") },
          QStringLiteral("April Native"),
          ImportDimension::Both)
{
}

ImportDimension NativeImportReader::dimension() const
{
    // NativeImportReader 同时支持 .apr (2D) 和 .apr3d (3D)
    // 由于无法在构造时预知会读取哪个文件，这里返回 Both
    // 实际过滤由调用方在读取时根据文件后缀判断
    return ImportDimension::Both;
}

ImportResult NativeImportReader::read(const ImportContext& context, Fio::VecAprEntityPtr& outEntities)
{
    // 检测是否为 3D 格式（.apr3d）
    QFileInfo fi(context.sourcePath);
    const QString suffix = fi.suffix().toLower();
    bool is3D = (suffix == QStringLiteral("apr3d"));
    Fio::FileFormat format = is3D ? Fio::FileFormat::Native3D : Fio::FileFormat::Native;

    APR_DEBUGF("[NativeImportReader] Importing native format: %s (format=%d)",
        is3D ? "3D (.apr3d)" : "2D (.apr)",
        static_cast<int>(format));

    // 使用新管线：parseDocument 获取完整 AprDocument，通过公开 API 提取图层/群组信息
    Fio::NativeParser parser(format);
    Fio::AprDocument doc;
    auto parseResult = parser.parseDocument(context.sourcePath.toUtf8().constData(), doc);

    if (!parseResult.success)
    {
        APR_ERRORF("[NativeImportReader] parseDocument failed: %s", parseResult.errorMessage.c_str());
        return ImportResult::fail(
            QString::fromStdString(parseResult.errorMessage), ImportErrorType::ParseFailed, QStringList{});
    }

    // 准备 ImportResult
    ImportResult result = ImportResult::ok(
        successMessage(format), static_cast<int>(doc.entityCount()), static_cast<int>(doc.layerCount()), {});

    // 工艺细节模型（ProcessModelData）：只在原生 2D .apr 里存在。
    // 读不到就保持 false —— 旧档案必须走「继承/关闭」，不能填默认值。
    if (!is3D)
    {
        result.hasProcessModel = Fio::ReadProcessModelFromDocument(doc, result.processModel);
    }

    // 1. 提取图层
    result.importedLayers.reserve(doc.layerCount());
    for (size_t i = 0; i < doc.layerCount(); ++i)
    {
        Fio::AprLayerInfo layerInfo;
        if (doc.getLayerAt(i, layerInfo))
        {
            Fio::IrLayerInfo irLayer;
            irLayer.sourceId = layerInfo.id;
            std::strncpy(irLayer.name, layerInfo.name, sizeof(irLayer.name) - 1);
            irLayer.color = layerInfo.color;
            irLayer.visible = layerInfo.visible;
            irLayer.locked = layerInfo.locked;
            // 加工语义（output / defaultProcessId）**不在 IrLayerInfo 里** ——
            // 那是冻结的跨 DLL POD（裸指针数组，步长即 ABI），
            // 且 DXF/SVG/PLT 这些外来格式根本没有这个概念。
            // 原生 .apr 的加工语义通过 ImportResult 的独立通道传递（见 ImportResult）。
            result.importedLayers.push_back(irLayer);

            // 加工语义走独立通道（ImportResult::LayerMachining）——
            // IrLayerInfo 是冻结的跨 DLL POD，不能加字段。
            ImportResult::LayerMachining machining;
            machining.sourceId = layerInfo.id;
            machining.output = layerInfo.output;
            machining.defaultProcessId = layerInfo.defaultProcessId;

            // 旧 POD 只有 6 个字段；硬件 Profile 绑定与填充/图层类型走新增的
            // AprLayerDetailInfo（另一个 POD，不动 AprLayerInfo 的 sizeof/ABI）。
            Fio::AprLayerDetailInfo detail;
            if (doc.getLayerDetailAt(i, detail))
            {
                machining.hardwareProfileId = detail.hardwareProfileId;
                machining.fillHardwareProfileId = detail.fillHardwareProfileId;
                machining.hasDisplayExtension = true;
                machining.layerType = detail.layerType;
                machining.fill = detail.fill;
                machining.fillColor = detail.fillColor;
            }
            result.importedLayerMachining.push_back(machining);
        }
    }

    // 1b. 提取硬件 Profile 值集。
    // LayerData.hardware_profile_id 只是引用，没有这组值就等于功率/速度/频率没存。
    // 旧文件（v2 之前）没有本字段 → 空值集；读端据此保持运行时现有 Profile，
    // 绝不静默清空用户参数。
    result.importedHardwareProfiles.reserve(doc.hardwareProfileCount());
    for (size_t i = 0; i < doc.hardwareProfileCount(); ++i)
    {
        Fio::AprHardwareProfileInfo profile;
        if (doc.getHardwareProfileAt(i, profile))
        {
            result.importedHardwareProfiles.push_back(profile);
        }
    }
    result.hasNativeMachiningSnapshot = !result.importedHardwareProfiles.empty();

    // 1c. 提取工艺对象（ProcessData）。
    //
    // 图层表只存 default_process_id 这个引用，真正的参数体在 ProcessData 里。
    // 旧文件 processCount()==0 → 空列表，读端保持现有运行时参数不动（与旧行为一致）。
    result.importedProcesses.reserve(doc.processCount());
    for (size_t i = 0; i < doc.processCount(); ++i)
    {
        Fio::AprProcessInfo info;
        if (!doc.getProcessAt(i, info))
        {
            continue;
        }

        ImportResult::ProcessBinding binding;
        binding.info = info;

        // 目标图层是变长数组，AprProcessInfo 是冻结 POD 装不下；
        // 先探长度再按长度取，避免固定容量截断导致「部分图层静默丢参数」。
        size_t targetCount = 0;
        if (doc.getProcessTargetLayers(info.id, nullptr, 0, &targetCount) && targetCount > 0)
        {
            binding.targetLayerIds.resize(targetCount);
            size_t written = 0;
            doc.getProcessTargetLayers(info.id, binding.targetLayerIds.data(), targetCount, &written);
            binding.targetLayerIds.resize(written);
        }
        result.importedProcesses.push_back(std::move(binding));
    }

    // 老版本写过图层 profile id 但没写值集：这些绑定是悬空的。
    // 只在确实引用了非默认 Profile 时提醒，避免每次打开旧文件都刷告警。
    if (!result.hasNativeMachiningSnapshot)
    {
        for (const auto& machining : result.importedLayerMachining)
        {
            if (machining.hardwareProfileId > 0 || machining.fillHardwareProfileId >= 0)
            {
                result.addWarning(QStringLiteral(
                    "This .apr file has layer profile bindings but no profile value set; "
                    "existing runtime profiles are kept and machining parameters may differ."));
                break;
            }
        }
    }

    // 2. 提取群组
    result.importedGroups.reserve(doc.groupCount());
    for (size_t i = 0; i < doc.groupCount(); ++i)
    {
        Fio::AprGroupInfo groupInfo;
        if (doc.getGroupAt(i, groupInfo))
        {
            Fio::IrGroupInfo irGroup;
            irGroup.sourceId = groupInfo.id;
            irGroup.parentSourceId = groupInfo.parentGroupId;
            std::strncpy(irGroup.name, groupInfo.name, sizeof(irGroup.name) - 1);
            result.importedGroups.push_back(irGroup);
        }
    }

    // 3. 计算导入侧 ID：直接沿用文档持久 ID，让 entityLayerMap / entityGroupMap
    //    的键与入库前的图元 ID 天然对齐。clone() 会生成新 ID（id 是身份不是状态），
    //    不还原就会让整批换层映射查不到图元，表现为「导入后全部落在默认图层」。
    //    缺失（<=0）或档案内重复的 ID 用批次内唯一的负数占位：SceneEditService
    //    遇到 <=0 会重新分配，并按「旧 ID -> 新 ID」修正映射，归属仍能落到正确图层。
    std::unordered_set<Eg::EntityId> usedIds;
    usedIds.reserve(doc.entityCount());
    std::vector<Eg::EntityId> importIds(doc.entityCount(), 0);
    for (size_t i = 0; i < doc.entityCount(); ++i)
    {
        Eg::AprEntity* entity = doc.entityAt(i);
        if (!entity)
        {
            continue;
        }
        Eg::EntityId id = entity->id;
        if (id <= 0 || !usedIds.insert(id).second)
        {
            id = -static_cast<Eg::EntityId>(i) - 1;
            while (!usedIds.insert(id).second)
            {
                --id;
            }
        }
        importIds[i] = id;
    }

    // 4. 建立图元 -> 图层映射
    result.entityLayerMap.reserve(doc.entityCount());
    for (size_t i = 0; i < doc.entityCount(); ++i)
    {
        Eg::AprEntity* entity = doc.entityAt(i);
        if (entity)
        {
            uint32_t layerId = doc.entityLayerId(entity->id);
            if (layerId != 0)
            {
                result.entityLayerMap[static_cast<int64_t>(importIds[i])] = layerId;
            }
        }
    }

    // 5. 建立图元 -> 群组映射
    for (size_t i = 0; i < doc.entityCount(); ++i)
    {
        Eg::AprEntity* entity = doc.entityAt(i);
        if (entity)
        {
            uint64_t groupId = doc.entityGroupId(entity->id);
            if (groupId != 0)
            {
                result.entityGroupMap[static_cast<int64_t>(importIds[i])] = groupId;
            }
        }
    }

    // 6. 移动图元到输出
    outEntities.clear();
    outEntities.reserve(doc.entityCount());
    for (size_t i = 0; i < doc.entityCount(); ++i)
    {
        // 这里无法直接移动，因为 entityAt 返回借用指针
        // 需要通过反序列化时已经存入文档的 unique_ptr
        // 但 AprDocument 公开 API 不支持移动所有权
        // 因此这里使用 clone
        Eg::AprEntity* entity = doc.entityAt(i);
        if (entity)
        {
            Eg::AprEntity* copy = entity->clone();
            copy->id = importIds[i];
            outEntities.emplace_back(copy);
        }
    }

    // 复制警告
    for (const auto& warn : parseResult.warnings)
    {
        result.addWarning(QString::fromStdString(warn));
    }

    APR_DEBUGF("[NativeImportReader] Imported via new pipeline: entities=%zu, layers=%zu, groups=%zu, layerMap=%zu, "
              "groupMap=%zu",
        outEntities.size(),
        result.importedLayers.size(),
        result.importedGroups.size(),
        result.entityLayerMap.size(),
        result.entityGroupMap.size());

    return result;
}

QString NativeImportReader::successMessage(Fio::FileFormat format) const
{
    return format == Fio::FileFormat::Native3D ? QStringLiteral("April 3D Native import successful")
                                               : QStringLiteral("April 2D Native import successful");
}