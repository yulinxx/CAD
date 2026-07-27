#include "StlImportReader.h"

#include "Engine3D/Loader/StlLoader.h"
#include "Log/SyLogger.h"

/**
 * @brief STL 文件导入实现
 * 使用 Eg::StlLoader 加载网格（支持 ASCII 和 Binary 格式），
 * 包装为 SyMeshEntity 后通过基类指针返回。
 * 日志关键位置：加载错误 / 成功
 */
ImportResult StlImportReader::read(const ImportContext& context,
    Fio::VecSyEntityPtr& outEntities)
{
    std::string error;
    auto mesh = Eg::StlLoader::load(context.sourcePath.toUtf8().toStdString(), error);

    if (!mesh)
    {
        QString msg = QString::fromStdString(error);
        SY_ERRORF("[StlImportReader] Failed to load STL file: %s (path=%s)",
            msg.toUtf8().constData(),
            context.sourcePath.toUtf8().constData());
        return ImportResult::fail(msg, ImportErrorType::ParseFailed);
    }

    SY_INFOF("[StlImportReader] STL loaded successfully: %s, triangles=%zu, verts=%zu",
        mesh->strName.c_str(), mesh->triangleCount(), mesh->vertices.size());

    // 通过基类指针存入输出列表
    outEntities.push_back(std::move(mesh));

    return ImportResult::ok(
        QStringLiteral("STL import successful: 1 mesh entity"),
        1);
}