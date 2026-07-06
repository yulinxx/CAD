#pragma once

#include "Render3D/IRenderer3D.h"
#include "RenderContext.h"

#include <memory>
#include <QPoint>

class SceneDocument3D;
class CameraController3D;
class SceneCompiler;
class IRenderBackend;
struct RenderContext;
struct RenderFrame;

/**
 * @file RenderCoreRenderer.h
 * @brief 基于 RenderCore 统一渲染管线的 IRenderer3D 适配器
 *
 * 桥接旧 IRenderer3D 接口与新 RenderCore 渲染管线。
 * 内部使用 SceneCompiler 编译场景、IRenderBackend 执行渲染。
 *
 * 两条渲染路径：
 * - 软件路径（默认）：SceneCompiler → RenderFrame → QPainter 直接绘制批次
 * - OpenGL 路径：SceneCompiler → IRenderBackend → FBO → 捕获 → QPainter 绘制
 *
 * 这是"最小可跑闭环"的核心组件，验证 SceneCompiler + IRenderBackend
 * 的抽象层是否真正可用。
 */
class RenderCoreRenderer : public IRenderer3D
{
public:
    RenderCoreRenderer();
    ~RenderCoreRenderer() override;

    // ========== IRenderer3D 接口实现 ==========

    bool initialize(void* windowHandle = nullptr) override;
    void shutdown() override;
    bool isReady() const override;
    void setRenderLoopEnabled(bool enabled) override;
    bool isRenderLoopRunning() const override;
    bool isOpenGL() const override;

    void setScene(SceneDocument3D* document) override;
    void setCamera(CameraController3D* controller) override;
    void render(QPainter& painter, int width, int height) override;
    void resize(int width, int height) override;
    void resetView() override;
    void setOrbitMode(bool enabled) override;
    void setMeasureMode(bool enabled) override;
    bool isOrbitMode() const override;

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
    /// 软件渲染路径：将 RenderFrame 的批次绘制到 QPainter
    void renderSoftware(QPainter& painter, const RenderFrame& frame);

    /// OpenGL 渲染路径：使用 IRenderBackend 渲染后捕获帧
    void renderOpenGL(QPainter& painter);

    /// 编译当前场景
    RenderFrame compileScene();

    /// 3D 点投影到 2D 屏幕坐标
    bool project3D(float x, float y, float z, int& sx, int& sy) const;

    /// 绘制 3D 场景批次（带投影）
    void renderBatches3D(QPainter& painter, const RenderFrame& frame);

    /// 绘制坐标轴指示器
    void drawAxesIndicator(QPainter& painter);

    /// 发射状态文本
    void emitStatus(const QString& text);

private:
    // 场景数据
    SceneDocument3D* m_document{ nullptr };
    CameraController3D* m_cameraController{ nullptr };

    // 渲染管线
    std::unique_ptr<SceneCompiler> m_compiler;
    std::unique_ptr<IRenderBackend> m_backend;
    RenderContext m_context;

    // 生命周期
    bool m_ready{ false };
    bool m_renderLoopEnabled{ false };
    bool m_useOpenGL{ false };

    // 视口
    int m_viewWidth{ 640 };
    int m_viewHeight{ 480 };

    // 相机状态（无外部 CameraController3D 时使用内置参数）
    double m_yaw{ 0.0 };
    double m_pitch{ 15.0 };
    double m_distance{ 10.0 };
    double m_panX{ 0.0 };
    double m_panY{ 0.0 };

    // 交互状态
    bool m_orbitMode{ true };
    bool m_measureMode{ false };
    bool m_rotating{ false };
    bool m_panning{ false };
    QPoint m_lastMousePos;

    // 选中状态
    QString m_selectedNodeId;
    QStringList m_selectedPathNames;

    // 回调
    StatusCallback m_statusCallback;
    SelectionCallback m_selectionCallback;
    PathCallback m_pathCallback;
};