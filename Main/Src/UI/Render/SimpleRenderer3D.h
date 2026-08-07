#pragma once

#include "Render3D/IRenderer3D.h"
#include "Render3D/ViewCamera3D.h"
#include "UI/IRenderSurface.h"  // P1: 2D/3D 公共渲染表面接口
#include <QString>

class SceneDocument3DAdapter;
class CameraController3D;

/**
 * @file SimpleRenderer3D.h
 * @brief 基于 QPainter 的最小 3D 软件渲染器
 *
 * 实现 IRenderer3D 接口，使用 QPainter 进行软件投影渲染。
 * 支持：坐标轴、线框立方体、轨道旋转、平移、缩放、命中测试。
 * 作为 3D 渲染链的最小可运行实现，验证从 UI 到渲染的完整链路。
 * 位于 Main 模块中作为默认渲染器，后续可替换为 RenderWidget3D。
 */

 /**
  * @class SimpleRenderer3D
  * @brief 最小 3D 软件渲染器
  *
  * 使用简单的透视投影将 3D 场景渲染到 QPainter 上。
  * 不依赖 OpenGL，仅使用 Qt 的 QPainter 绘制。
  */
class SimpleRenderer3D : public IRenderer3D
    , public UI::IRenderSurface  // P1: 实现 2D/3D 公共渲染表面接口
{
public:
    SimpleRenderer3D();
    ~SimpleRenderer3D() override;

public:
    // ========== IRenderer3D 接口实现 ==========

    bool initialize(void* windowHandle = nullptr) override;
    void shutdown() override;
    bool isReady() const override;
    void setRenderLoopEnabled(bool enabled) override;
    bool isRenderLoopRunning() const override;

    void setScene(SceneDocument3DAdapter* document) override;
    void setCamera(CameraController3D* controller) override;
    void render(QPainter& painter, int width, int height) override;
    void resize(int width, int height) override;
    void resetView() override;
    void setOrbitMode(bool enabled) override;
    void setMeasureMode(bool enabled) override;
    bool isOrbitMode() const override;
    bool isOpenGL() const override;

    void onMousePress(int x, int y, int button, int modifiers, int viewW, int viewH) override;
    void onMouseMove(int x, int y, int buttons, int viewW, int viewH) override;
    void onMouseRelease(int x, int y, int button, int viewW, int viewH) override;
    void onWheel(int delta, int viewW, int viewH) override;

    void selectNodeById(const QString& nodeId) override;
    QString selectedNodeId() const override;
    QStringList selectedPathNames() const override;

    void setStatusCallback(StatusCallback callback) override;
    void setSelectionCallback(SelectionCallback callback) override;
    void setPathCallback(PathCallback callback) override;

private:
    /// 3D 点投影到 2D 屏幕坐标
    bool project(float x, float y, float z, int& sx, int& sy) const;

    /// 绘制坐标轴
    void drawAxes(QPainter& painter);

    /// 绘制线框立方体
    void drawWireCube(QPainter& painter, float cx, float cy, float cz, float halfSize);

    /// 绘制场景节点
    void drawSceneNodes(QPainter& painter);

    /// 绘制选中节点路径叠加信息
    void drawNodePathOverlay(QPainter& painter);

    /// 发射状态文本
    void emitStatus(const QString& text);

    /// 按屏幕坐标命中测试场景节点
    QString hitTest(int screenX, int screenY) const;

    /// 重建选中节点的路径缓存
    void rebuildTreeHighlight();

private:
    SceneDocument3DAdapter* m_document{ nullptr };
    CameraController3D* m_cameraController{ nullptr };

    bool m_ready{ false };
    bool m_renderLoopEnabled{ false };

    ViewCamera3D m_camera;

    QString m_selectedNodeId;
    QStringList m_selectedPathNames;

    StatusCallback m_statusCallback;
    SelectionCallback m_selectionCallback;
    PathCallback m_pathCallback;
};