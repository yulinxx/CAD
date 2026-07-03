/**
 * @file UiSelectionTools.h
 * @brief UI 选择工具 — 修剪、延伸、变换等选择集操作
 */

#pragma once

#include <QPointF>
#include <QString>

class EntityDocument2D;
class UiStateCenter;

/// UI 层选择集操作工具集，供 2D 视口的交互命令调用
namespace UiSelectionTools
{
    /// 修剪：将选中线段的最近端点裁剪到点击位置的投影点
    void trimSelectedByPoint(EntityDocument2D* document, const QPointF& point, UiStateCenter* stateCenter);

    /// 延伸：将选中线段的最近端点延伸到点击位置的投影点（当前委托给 trim）
    void extendSelectedByPoint(EntityDocument2D* document, const QPointF& point, UiStateCenter* stateCenter);

    /// 对选中实体应用平移变换；transformCopy 为 true 时保留原对象并创建副本
    void applySelectionTransform(EntityDocument2D* document, const QPointF& anchor, const QPointF& target, bool transformCopy, const QString& mode, UiStateCenter* stateCenter, const QString& toolName);
}
