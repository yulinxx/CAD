#pragma once

#include "RenderFrame.h"
#include "UiCamera3D.h"

#include <QPainter>

/**
 * @file SoftwareRenderer.h
 * @brief 软件渲染器实现
 *
 * 负责将 RenderFrame 的批次数据绘制到 QPainter，提供纯 CPU 渲染路径。
 *
 * 职责边界：
 * - 清屏与背景填充
 * - 3D 批次投影与绘制（线、点、折线）
 * - UI 覆盖层绘制（坐标轴指示器、统计信息）
 * - 选中状态渲染反馈
 *
 * 不承担：
 * - 场景编译（由 SceneCompiler 负责）
 * - 相机交互（由 UiCamera3D 负责）
 * - 输入事件处理（由上层负责）
 * - 生命周期管理（由调用方负责）
 */
class SoftwareRenderer
{
public:
    /// 将渲染帧绘制到 QPainter
    void render(QPainter& painter, const RenderFrame& frame, const UiCamera3D& camera,
                const QSize& viewportSize);

private:
    /// 绘制 3D 批次（使用 Camera3D 投影）
    void drawBatches3D(QPainter& painter, const RenderFrame& frame, const UiCamera3D& camera);

    /// 绘制坐标轴指示器
    static void drawAxesIndicator(QPainter& painter, const UiCamera3D& camera,
                                   int viewW, int viewH);

    /// 绘制帧统计覆盖层
    static void drawStatisticsOverlay(QPainter& painter, const RenderFrame& frame,
                                       int viewW, int viewH);
};