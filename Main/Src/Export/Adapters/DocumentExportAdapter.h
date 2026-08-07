#pragma once

#include <memory>

#include "FileIO/FileFormat.h"
#include "FileIO/IFileParser.h"

namespace Eg
{
    class SceneManager;
}

/// 文档导出适配器：从场景文档收集图元数据供导出使用
class DocumentExportAdapter
{
public:
    explicit DocumentExportAdapter(Eg::SceneManager* sceneManager);

public:
    /// 收集 2D 场景中的所有图元
    /// @return 图元列表（克隆的副本，原场景不受影响）
    Fio::VecSyEntityPtr collect2D();

    /// 收集 3D 场景中的所有图元
    /// @return 图元列表
    Fio::VecSyEntityPtr collect3D();

    /// 收集当前选中的图元
    /// @return 仅选中图元的列表
    Fio::VecSyEntityPtr collectSelected();

private:
    /// 场景管理器（非拥有指针）
    Eg::SceneManager* m_sceneManager{ nullptr };
};
