#pragma once

#include <memory>

#include <QString>

#include "FileIO/FileFormat.h"
#include "FileIO/IFileParser.h"

namespace Eg
{
    class SceneManager;
    struct SyEntity;
}  // namespace Eg
class SceneEditService;

/// 文档导入适配器：将导入的图元数据落地到场景文档
class DocumentImportAdapter
{
public:
    explicit DocumentImportAdapter(Eg::SceneManager* sceneManager, SceneEditService* editService = nullptr);

public:
    /// 将导入的图元应用到 2D 场景文档
    /// @param entities 导入的图元列表
    /// @param preserveColors 是否保留源文件颜色
    /// @param preserveLayers 是否保留源文件图层
    /// @return 成功添加的图元数量
    int apply2D(Fio::VecSyEntityPtr& entities, bool preserveColors = true, bool preserveLayers = true);

    /// 将导入的图元应用到 3D 场景文档
    /// @param entities 导入的图元列表
    /// @return 成功添加的图元数量
    int apply3D(Fio::VecSyEntityPtr& entities);

private:
    /// 场景管理器（非拥有指针）
    Eg::SceneManager* m_sceneManager{ nullptr };
    /// 场景编辑服务（非拥有指针，支持 Undo 时使用）
    SceneEditService* m_editService{ nullptr };
};
