#include "Runtime/CADApplicationRuntime.h"

#include <QSurfaceFormat>

int runCADApplication(int argc, char** argv);

int main(int argc, char** argv)
{
    // macOS: QOpenGLWidget must share its framebuffer texture with Qt's
    // internal compositing context. If the widget sets a format that differs
    // from the global default (e.g. 4.1 CoreProfile), Qt emits
    // "Could not create NSOpenGLContext with shared context, falling back to
    // unshared context" and the widget renders black.
    // Setting the SAME format as the global default makes the contexts shareable.
    //
    // Windows: RenderWidget3D 仍使用固定管线（glBegin/glMatrixMode/glFrustum 等），
    // 在 CoreProfile 下这些函数会被禁用导致 3D 网格与模型不显示。
    // 因此 Windows 采用 CompatibilityProfile 兼容现代 shader 与固定管线。
    QSurfaceFormat format;
#ifdef Q_OS_MACOS
    format.setVersion(4, 1);
    format.setProfile(QSurfaceFormat::CoreProfile);
#else
    format.setVersion(4, 6);
    format.setProfile(QSurfaceFormat::CompatibilityProfile);
#endif
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    format.setSamples(0);
    format.setSwapInterval(1);
    QSurfaceFormat::setDefaultFormat(format);

    return runCADApplication(argc, argv);
}