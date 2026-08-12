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
    QSurfaceFormat format;
#ifdef Q_OS_MACOS
    format.setVersion(4, 1);
#else
    format.setVersion(4, 6);
#endif
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    format.setSamples(0);
    format.setSwapInterval(1);
    QSurfaceFormat::setDefaultFormat(format);

    return runCADApplication(argc, argv);
}
