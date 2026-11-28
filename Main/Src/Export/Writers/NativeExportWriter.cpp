/**
 * @file NativeExportWriter.cpp
 * @brief 原生格式导出写入器实现
 */
#include "NativeExportWriter.h"

#include "FileIO/Writers/NativeWriter.h"
#include "FileIO/AprDocument.h"
#include "FileIO/ProcessModelSnapshot.h"
#include "Engine2D/Core/SceneManager.h"
#include "Engine3D/SceneManager3D.h"
#include "Engine3D/AprEntity/AprMeshEntity.h"
#include "Log/AprLogger.h"

// forEachEntity 回调：将实体添加到 AprDocument
static void addMeshEntityToDoc(Eg::AprMeshEntity* entity, void* ctx)
{
    if (entity && ctx)
    {
        auto* doc = static_cast<Fio::AprDocument*>(ctx);
        doc->addBorrowedEntity(entity);
    }
}

/**
 * @brief 加工语义快照没采到时的告警。
 *
 * ExportService 在导出 Phase 1（主线程）采集 NativeDocumentSnapshot，本写入器把它写进
 * AprDocument。组合根没注入 provider、或快照采集抛异常时 hasMachiningSnapshot 保持 false，
 * 必须显式告警：宁可吵，也不能让操作员以为参数已经落盘。
 */
static QString nativeMachiningSnapshotMissingWarning()
{
    return QStringLiteral(
        "Native .apr export saved entities only: the layer table and hardware-profile value set "
        "were not captured. Reopening this file restores default machining parameters.");
}

/// 图层表写进去了但 Profile 值集为空（典型原因：HardwareProfileManager 尚未注入
/// LayerManager）。图层上的 hardware_profile_id 只是**引用**，没有值集同样等于
/// 参数没存下来，必须单独告警，不能笼统地说「已保存」。
static QString nativeProfileValueSetMissingWarning()
{
    return QStringLiteral(
        "Native .apr export saved the layer table but no hardware-profile value set: "
        "layer profile bindings point at runtime defaults after reopening.");
}

/// 把主线程采到的加工语义快照写进文档。两个出参分开记录，便于只缺值集时
/// 给出精确告警，而不是笼统地说「都没存」。
static void applyMachiningSnapshot(
    const ExportContext& context, Fio::AprDocument& doc, bool& layersWritten, bool& profilesWritten)
{
    layersWritten = false;
    profilesWritten = false;
    if (!context.hasMachiningSnapshot)
    {
        return;
    }

    for (const auto& layer : context.machiningSnapshot.layers)
    {
        doc.addLayer(layer);
    }
    for (const auto& profile : context.machiningSnapshot.profiles)
    {
        doc.addHardwareProfile(profile);
    }
    for (const auto& process : context.machiningSnapshot.processes)
    {
        doc.addProcess(process.info);
        // 显式目标图层走独立 API：AprProcessInfo 是 POD，装不下变长数组
        for (uint32_t layerId : process.targetLayerIds)
        {
            doc.addProcessTargetLayer(process.info.id, layerId);
        }
    }
    if (!context.machiningSnapshot.machiningLayerOrder.empty())
    {
        doc.setMachiningLayerOrder(context.machiningSnapshot.machiningLayerOrder.data(),
            context.machiningSnapshot.machiningLayerOrder.size());
    }
    if (context.machiningSnapshot.hasProcessModel)
    {
        Fio::WriteProcessModelToDocument(doc, context.machiningSnapshot.processModel);
    }

    layersWritten = !context.machiningSnapshot.layers.empty();
    profilesWritten = !context.machiningSnapshot.profiles.empty();
}

NativeExportWriter::NativeExportWriter()
    : ExportWriterBase(
          Fio::FileFormat::Native, { QStringLiteral("apr") }, QStringLiteral("April Native"), QStringLiteral("apr"))
{
}

ExportResult NativeExportWriter::write(const ExportContext& context, const Fio::VecAprEntityPtr& entities)
{
    const Fio::FileFormat fmt = resolveFormat(entities);

    Fio::AprDocument doc;

    // ---- 图元：优先借用场景里的原对象，避免深拷贝 ----
    if (context.sceneManager)
    {
        auto allEntities = context.sceneManager->getAllEntities();
        for (auto* e : allEntities)
        {
            if (e)
            {
                doc.addBorrowedEntity(e);
            }
        }

        // 借用 3D 网格实体（如有）
        if (context.sceneManager3D && fmt == Fio::FileFormat::Native3D)
        {
            context.sceneManager3D->forEachEntity(addMeshEntityToDoc, &doc);
        }
    }
    else
    {
        // 没有 sceneManager：用导出服务已经克隆好的图元。两条路径统一由 NativeWriter
        // 写同一份 AprDocument（不走 FileIOManager::exportFile，那条链没有图层表入口）。
        for (const auto& e : entities)
        {
            if (e)
            {
                doc.addBorrowedEntity(e.get());
            }
        }
    }

    // ---- 加工语义：图层表 + 硬件 Profile 值集 ----
    // 快照是主线程采好、按值带进来的 POD；后台线程只做写入，不碰任何 Manager。
    bool layersWritten = false;
    bool profilesWritten = false;
    applyMachiningSnapshot(context, doc, layersWritten, profilesWritten);

    Fio::NativeWriter writer(fmt);
    // 路径按 UTF-8 全量传递（不截断到固定长度缓冲区）
    const QByteArray targetPathUtf8 = context.targetPath.toUtf8();
    // writeDocumentWithMetadata 由 MetadataFiller 填好软件名/版本/时间戳/溯源信息
    const auto writeResult = writer.writeDocumentWithMetadata(targetPathUtf8.constData(), doc);
    if (!writeResult.success)
    {
        const QString msg = QString::fromUtf8(writeResult.errorMessage);
        APR_ERRORF("[NativeExportWriter] Write failed: %s", msg.toUtf8().constData());
        return ExportResult::fail(msg);
    }

    auto exportResult = ExportResult::ok(successMessage(), static_cast<int>(entities.size()));
    if (!layersWritten)
    {
        exportResult.addWarning(nativeMachiningSnapshotMissingWarning());
    }
    else if (!profilesWritten)
    {
        exportResult.addWarning(nativeProfileValueSetMissingWarning());
    }
    APR_DEBUGF("[NativeExportWriter] .apr saved: %zu layer(s), %zu profile(s), %zu process(es), %d entit(y/ies)",
        context.machiningSnapshot.layers.size(),
        context.machiningSnapshot.profiles.size(),
        context.machiningSnapshot.processes.size(),
        static_cast<int>(entities.size()));
    return exportResult;
}

Fio::FileFormat NativeExportWriter::resolveFormat(const Fio::VecAprEntityPtr& entities) const
{
    // 检测是否包含 3D 网格图元，据此选择 3D/2D 原生格式
    for (const auto& entity : entities)
    {
        if (entity && entity->eType == Eg::EType::MESH)
        {
            return Fio::FileFormat::Native3D;
        }
    }
    return Fio::FileFormat::Native;
}

QString NativeExportWriter::successMessage() const
{
    return QStringLiteral("Native export successful");
}