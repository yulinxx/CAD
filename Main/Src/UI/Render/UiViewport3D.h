#pragma once

#include <QStringList>
#include <QWidget>

#include <memory>

class CameraController3D;
class QContextMenuEvent;
class QMouseEvent;
class QPaintEvent;
class QResizeEvent;
class QWheelEvent;
class IRenderer3D;
class SceneDocument3D;

class Viewport3D final : public QWidget
{
    Q_OBJECT
public:
    explicit Viewport3D(QWidget* parent = nullptr);
    ~Viewport3D() override;

    void setRenderer(std::unique_ptr<IRenderer3D> renderer);
    bool initialize(void* windowHandle = nullptr);
    void setStatusCallback(std::function<void(const QString&)>&& callback);
    void setSceneDocument(SceneDocument3D* document);
    void setCameraController(CameraController3D* controller);
    void setSelectionCallback(std::function<void(const QString&)>&& callback);
    void setPathCallback(std::function<void(const QStringList&)>&& callback);
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
    std::unique_ptr<IRenderer3D> m_renderer;
};
