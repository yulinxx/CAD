#pragma once

#include <QString>

class WorkbenchWindow;

/**
 * @file UiLayoutService.h
 * @brief 布局服务接口定义
 *
 * 定义了 UI 布局服务接口，负责管理窗口布局的保存和恢复。
 */

 /**
  * @class UiLayoutService
  * @brief 布局服务抽象接口
  *
  * 提供布局快照的保存和恢复功能，支持不同工作台的布局管理。
  */
class UiLayoutService
{
public:
    virtual ~UiLayoutService() = default;

    /// 保存布局快照
    /// @param workbenchId 工作台 ID
    /// @param window 主窗口指针
    virtual void saveLayout(const QString& workbenchId, WorkbenchWindow* window) = 0;

    /// 恢复布局快照
    /// @param workbenchId 工作台 ID
    /// @param window 主窗口指针
    virtual void restoreLayout(const QString& workbenchId, WorkbenchWindow* window) = 0;
};

/**
 * @class DefaultUiLayoutService
 * @brief 默认布局服务实现
 *
 * 使用 QSettings 保存和恢复窗口布局，基于工作台 ID 进行区分。
 */
class DefaultUiLayoutService final : public UiLayoutService
{
public:
    void saveLayout(const QString& workbenchId, WorkbenchWindow* window) override;
    void restoreLayout(const QString& workbenchId, WorkbenchWindow* window) override;
};
