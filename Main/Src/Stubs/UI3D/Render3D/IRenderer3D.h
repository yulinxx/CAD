#pragma once
#include <QString>
#include <QStringList>
#include <QPainter>
#include <functional>
class SceneDocument3DAdapter;
class CameraController3D;

class IRenderer3D 
{
public:
  virtual ~IRenderer3D() = default;


  using StatusCallback = std::function<void(const QString&)>;
  using SelectionCallback = std::function<void(const QString&)>;
  using PathCallback = std::function<void(const QStringList&)>;

  public:
  virtual bool initialize(void* = nullptr) = 0;
  virtual void shutdown() = 0;
  virtual bool isReady() const = 0;
  virtual void setRenderLoopEnabled(bool) = 0;
  virtual bool isRenderLoopRunning() const = 0;
  virtual void setScene(SceneDocument3DAdapter*) = 0;
  virtual void setCamera(CameraController3D*) = 0;
  virtual void render(QPainter&, int, int) = 0;
  virtual void resize(int, int) = 0;
  virtual void resetView() = 0;
  virtual void setOrbitMode(bool) = 0;
  virtual void setMeasureMode(bool) = 0;
  virtual bool isOrbitMode() const = 0;
  virtual bool isOpenGL() const = 0;
  virtual void selectNodeById(const QString&) = 0;
  virtual QString selectedNodeId() const = 0;
  virtual QStringList selectedPathNames() const = 0;
  virtual void setStatusCallback(StatusCallback) = 0;
  virtual void setSelectionCallback(SelectionCallback) = 0;
  virtual void setPathCallback(PathCallback) = 0;
};
