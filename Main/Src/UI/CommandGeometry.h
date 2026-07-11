#pragma once

#include "UiCommandHandler.h"
#include "Ut/Vec.h"

namespace Eg {
    class SyEntity;
}

/**
 * @file CommandGeometry.h
 * @brief 命令几何变换辅助函数
 *
 * 提供镜像、旋转等几何变换的辅助功能。
 */

/**
 * 旋转一个点（弧度）
 * @param point 原始点
 * @param center 旋转中心
 * @param cosAngle 角度余弦值
 * @param sinAngle 角度正弦值
 * @return 旋转后的点
 */
QPointF rotatePoint(const QPointF& point, const QPointF& center, double cosAngle, double sinAngle);

/**
 * 将 QPointF 转换为 Ut::Vec2d
 */
inline Ut::Vec2d toVec2d(const QPointF& p);

/**
 * 镜像一个点：关于 P1→P2 轴镜像
 * @param pt 原始点
 * @param axisStart 镜像轴起点
 * @param axisEnd 镜像轴终点
 * @return 镜像后的点
 */
QPointF mirrorPoint(const QPointF& pt, const QPointF& axisStart, const QPointF& axisEnd);

/**
 * 对实体应用镜像变换（就地修改）
 * @param entity 目标实体
 * @param axisStart 镜像轴起点
 * @param axisEnd 镜像轴终点
 */
void applyMirrorToEntity(Eg::SyEntity* entity, const QPointF& axisStart, const QPointF& axisEnd);

/**
 * 对实体应用旋转变换（就地修改）
 * @param entity 目标实体
 * @param center 旋转中心
 * @param angleDelta 旋转角度（弧度）
 */
void applyRotationToEntity(Eg::SyEntity* entity, const QPointF& center, double angleDelta);