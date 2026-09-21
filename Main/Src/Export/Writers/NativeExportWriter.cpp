/**
 * @file NativeExportWriter.cpp
 * @brief 原生格式导出写入器实现
 */
#include "NativeExportWriter.h"

#include "FileIO/Writers/NativeWriter.h"
#include "FileIO/SyDocument.h"
#include "Engine2D/Core/SceneManager.h"
#include "Engine3D/SceneManager3D.h"
#include "Engine3D/SyEntity/SyMeshEntity.h"
#include "Log/SyLogger.h"

// forEachEntity 回调：将实体添加到 SyDocument
static void addMeshEntityToDoc(Eg::SyMeshEntity* entity, void* ctx)
{
    if (entity && ctx)
    {
        auto* doc = static_cast<Fio::SyDocument*>(ctx);
        doc->addBorrowedEntity(entity);
    }
}

NativeExportWriter::NativeExportWriter()
    : ExportWriterBase(
          Fio::FileFormat::Native, { QStringLiteral("sy") }, QStringLiteral("SanYi Native"), QStringLiteral("sy"))
{
}

ExportResult NativeExportWriter::write(const ExportContext& context, const Fio::VecSyEntityPtr& entities)
{
    // 优先使用借用实体路径（当 sceneManager 可用时）
    if (context.sceneManager)
    {
        const Fio::FileFormat fmt = resolveFormat(entities);

        // 构建 SyDocument，借用实体避免深拷贝
        Fio::SyDocument doc;

        // 借用 2D 实体
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

        // 直接使用 NativeWriter 写入（NativeWriter 内部通过 MetadataFiller 填充元数据）
        Fio::NativeWriter writer(fmt);
        char pathBuf[1024] = { 0 };
        auto pathStr = context.targetPath.toUtf8();
        std::memcpy(pathBuf, pathStr.constData(), std::min(static_cast<size_t>(pathStr.size()), sizeof(pathBuf) - 1));

        auto result = writer.writeDocument(pathBuf, doc);
        if (!result.success)
        {
            QString msg = QString::fromUtf8(result.errorMessage);
            SY_ERRORF("[NativeExportWriter] Write failed: %s", msg.toUtf8().constData());
            return ExportResult::fail(msg);
        }

        return ExportResult::ok(successMessage(), static_cast<int>(entities.size()));
    }

    // 降级：使用父类默认实现（clone 方式，向后兼容）
    return ExportWriterBase::write(context, entities);
}

Fio::FileFormat NativeExportWriter::resolveFormat(const Fio::VecSyEntityPtr& entities) const
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