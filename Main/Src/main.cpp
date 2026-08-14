#include "Runtime/CADApplicationRuntime.h"

#include <QSurfaceFormat>

int runCADApplication(int argc, char** argv);

int main(int argc, char** argv)
{
    // 全应用统一采用现代 OpenGL（CoreProfile），不使用固定管线：
    // - Windows：OpenGL 4.6 CoreProfile（最高版本）
    // - macOS：OpenGL 4.1 CoreProfile（macOS 支持的最高版本）
    // 所有渲染（2D Renderx、3D 视图）均使用 shader + VBO/VAO 编程。
    //
    // macOS 要求 QOpenGLWidget 与全局默认格式一致，否则 NSOpenGLContext
    // 无法共享 framebuffer，widget 会渲染黑屏/错位。故各 widget 不再单独
    // 覆盖格式，统一沿用此处全局默认。
    QSurfaceFormat format;
#ifdef Q_OS_MACOS
    format.setVersion(4, 1);
    format.setProfile(QSurfaceFormat::CoreProfile);
#else
    format.setVersion(4, 6);
    format.setProfile(QSurfaceFormat::CoreProfile);
#endif
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    format.setSamples(0);
    format.setSwapInterval(1);
    QSurfaceFormat::setDefaultFormat(format);

    return runCADApplication(argc, argv);
}