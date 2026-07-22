#pragma once

#include <memory>

#include "FileIO/FileFormat.h"

namespace Eg { class SceneManager; }

/// 文档导出适配器：从场景文档收集实体数据供导出使用
class DocumentExportAdapter
{
public:
    explicit DocumentExportAdapter(Eg::SceneManager* sceneManager);

    /// 收集 2D 场景中的所有实体
    /// @return 实体列表（克隆的副本，原场景不受影响）
    Fio::VecSyEntityPtr collect2D();

    /// 收集 3D 场景中的所有实体
    /// @return 实体列表
    Fio::VecSyEntityPtr collect3D();

    /// 收集当前选中的实体
    /// @return 仅选中实体的列表
    Fio::VecSyEntityPtr collectSelected();

private:
    /// 场景管理器（非拥有指针）
    Eg::SceneManager* m_sceneManager{ nullptr };
};
