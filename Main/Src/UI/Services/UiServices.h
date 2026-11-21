#pragma once

#include <functional>
#include <memory>

#include "UiServiceGroups.h"

/**
 * @struct UiServices
 * @brief UI 服务集合（分组聚合）
 *
 * 组合根的装配容器：由 ApplicationCompositionRoot 填充，唯一存储是七个
 * 聚焦分组（UiState/Command/Scene/Persistence/View/Algorithm/Settings，定义见
 * UiServiceGroups.h）。消费者通过 uiState/commands/scene/persistence/view/algorithm/settings
 * 访问，不再有扁平字段——分组是唯一事实源，新增服务只需在一个分组加字段、
 * 装配处赋值一次，消除此前扁平字段与分组访问器双重维护导致的字段不同步风险。
 *
 * 【已收口（P2）】消费者之间不再传递本聚合：
 *  - 只依赖抽象服务的消费者直接依赖对应接口（如 WorkbenchMenuManager → IRecentFileService）
 *  - 需要多个服务的消费者依赖聚焦分组（UiState/Command/Scene/Persistence/View）
 *  - 工作台接口 UiWorkbench::initialize 收 WorkbenchServices bundle
 * 本聚合仅由 ApplicationCompositionRoot 填充，作为**装配容器**使用。
 */
struct UiServices
{
    // 唯一存储：七个聚焦分组。新增服务只在对应分组（UiServiceGroups.h）里加一个
    // 字段，装配处赋值一次。此前本结构同时持有扁平字段与分组访问器，同一组指针
    // 存两份结构定义，加服务要改两处、字段增删极易不同步（已造成过死字段残留）；
    // 现在分组是唯一事实源，消费者通过 uiState/commands/scene/persistence/view/
    // algorithm/settings 访问。
    UiStateServices uiState;
    CommandServices commands;
    SceneServices scene;
    PersistenceServices persistence;
    ViewServices view;
    AlgorithmServices algorithm;
    SettingsServices settings;

    /// 工作台装配集合（供 UiWorkbench::initialize 使用，避免其接口依赖本聚合）
    WorkbenchServices workbench() const
    {
        return { uiState, commands, scene, persistence, view, algorithm, settings };
    }
};