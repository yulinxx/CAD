/**
 * @file UiGeometryAlgorithms.h
 * @brief UI 几何算法 — 点投影、旋转、镜像等基础几何运算
 */

#pragma once

#include <QPointF>

 /// UI 层几何算法工具集，供 2D 视口的变换操作使用
namespace UiGeometryAlgorithms
{
    /// 将点投影到线段 AB 上（最近点），结果限制在线段范围内
    QPointF projectPointToLine(const QPointF& point, const QPointF& a, const QPointF& b);

    /// 将点绕锚点逆时针旋转 90 度
    QPointF rotatePoint90(const QPointF& p, const QPointF& anchor);

    /// 将点关于锚点做垂直镜像（X 轴翻转）
    QPointF mirrorPointVertical(const QPointF& p, const QPointF& anchor);
}
