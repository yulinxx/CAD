#pragma once

#include <string>

/**
 * @brief 工作台布局快照记录 — 数据库持久化模型
 *
 * 保存主窗口几何位置、窗口状态（最大化/全屏等）和 dock 布局。
 * 由 WorkspaceSnapshotRepository 读写，WorkbenchWindow 在切换工作台时调用。
 */
struct WorkspaceSnapshotRecord
{
    int id{ 0 };                    // 自增主键
    std::string workbenchId;        // 工作台标识（如 "2D", "3D"）
    std::string geometry;           // QWidget::saveGeometry() 的 Base64 编码
    std::string windowState;        // QWidget::saveState() 的 Base64 编码
    std::string updatedAt;          // 最近更新时间（ISO 8601 格式）
};