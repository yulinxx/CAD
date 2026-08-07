#pragma once

#include <QStringList>
#include <QWidget>
#include <QEvent>

#include <functional>
#include <memory>

class CameraController3D;
class QContextMenuEvent;
class QMouseEvent;
class QPaintEvent;
class QResizeEvent;
class QWheelEvent;
class IRenderer3D;
class SceneDocument3DAdapter;

class Viewport3D final : public QWidget
{
    Q_OBJECT
public:
    explicit Viewport3D(QWidget* parent = nullptr);
    ~Viewport3D() override;

public:
    void setRenderer(std::unique_ptr<IRenderer3D> renderer);
    bool initialize(void* windowHandle = nullptr);

    void setStatusCallback(std::function<void(const QString&)> callback);
    void setSceneDocument(SceneDocument3DAdapter* document);
    void setCameraController(CameraController3D* controller);
    void setSelectionCallback(std::function<void(const QString&)> callback);
    void setPathCallback(std::function<void(const QStringList&)> callback);
    void setInputHandler(std::function<bool(QEvent* event)> handler);

    /// 在 native window 销毁前显式释放渲染器资源，避免析构时访问无效句柄崩溃
    void releaseGLResources();

    void resetCamera();
    void setOrbitMode(bool enabled);
    void setMeasureMode(bool enabled);

    QString selectedNodeId() const;
    void selectNodeById(const QString& nodeId);
    QStringList selectedPathNames() const;
    bool isUsingOpenGL() const;

    // 获取当前渲染器（供外部访问内部适配器使用）
    IRenderer3D* renderer() const;
    bool isRendererReady() const;

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    std::function<bool(QEvent* event)> m_inputHandler;
    std::unique_ptr<IRenderer3D> m_renderer;
};
