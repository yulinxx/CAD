#include "ObjImportReader.h"

#include "Engine3D/Loader/ObjLoader.h"
#include "Log/SyLogger.h"

/**
 * @brief OBJ 文件导入实现
 * 使用 Eg::ObjLoader 加载网格，包装为 SyMeshEntity 后通过基类指针返回。
 * 日志关键位置：加载错误 / 成功
 */
ImportResult ObjImportReader::read(const ImportContext& context, Fio::VecSyEntityPtr& outEntities)
{
    std::string error;
    auto mesh = Eg::ObjLoader::load(context.sourcePath.toUtf8().toStdString(), error);

    if (!mesh)
    {
        QString msg = QString::fromStdString(error);
        SY_ERRORF("[ObjImportReader] Failed to load OBJ file: %s (path=%s)",
            msg.toUtf8().constData(),
            context.sourcePath.toUtf8().constData());
        return ImportResult::fail(msg, ImportErrorType::ParseFailed);
    }

    SY_INFOF("[ObjImportReader] OBJ loaded successfully: %s, triangles=%zu, verts=%zu",
        mesh->name(),
        mesh->triangleCount(),
        mesh->vertices.size());

    // 通过基类指针存入输出列表（IImportReader 接口使用 SyEntity 基类）
    outEntities.push_back(std::move(mesh));

    return ImportResult::ok(QStringLiteral("OBJ import successful: 1 mesh entity"), 1);
}