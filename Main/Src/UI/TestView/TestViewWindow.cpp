#include "TestViewWindow.h"

#include "RenderWidget.h"
#include "UI/Render/Camera2D.h"

#include "Engine/Scene/SceneRenderContract.h"

#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QWheelEvent>
#include <QTimer>

namespace
{
    /// 触控板/滚轮手势分类（与主视口 ViewportInputRouter 保持一致）
    enum class TestViewGesture
    {
        Zoom,
        Pan,
        HorizontalPan,
    };

    TestViewGesture classifyWheel(Qt::ScrollPhase phase, Qt::KeyboardModifiers modifiers)
    {
        // 鼠标滚轮：滚轮=缩放，不因修饰键改变
        if (phase == Qt::NoScrollPhase)
        {
            return TestViewGesture::Zoom;
        }
        // 仅触控板（带滚动阶段）
        if (modifiers.testFlag(Qt::ControlModifier))
        {
            return TestViewGesture::Zoom;
        }
        if (modifiers.testFlag(Qt::ShiftModifier))
        {
            return TestViewGesture::HorizontalPan;
        }
        return TestViewGesture::Pan;
    }
}  // namespace

/**
 * @brief 仅显示用渲染控件
 *
 * 继承 RenderWidget 并关闭场景环境(幅面/网格/标尺)，只负责把场景图元画出来。
 * 导航与主视口一致：滚轮缩放、触控板双指平移/捏合缩放、左/中键拖拽平移；
 * 不实现任何选择/编辑逻辑。
 */
class TestViewRenderWidget : public RenderWidget
{
public:
    explicit TestViewRenderWidget(QWidget* parent = nullptr)
        : RenderWidget(parent)
    {
        // 仅显示图元：关闭工作台(幅面)、网格、标尺等场景环境，
        // 使 computeGeometry 返回空，渲染时完全跳过这些背景元素。
        if (auto* env = sceneEnvironment())
        {
            env->setTableVisible(false);
            env->setGridVisible(false);
            env->setRulerVisible(false, false);
        }
    }

    void setScene(Eg::ISceneDataSource* scene)
    {
        m_scene = scene;
    }

    /// OpenGL 初始化完成后加载场景并适配视图（未就绪则延迟重试）
    void finishInit()
    {
        if (!m_scene)
        {
            return;
        }
        if (!isInitialized())
        {
            QTimer::singleShot(16, this, [this]() { finishInit(); });
            return;
        }

        submitSceneFromDataSource(m_scene);
        fitScene();
    }

    void fitScene()
    {
        const float vpW = physicalWidth();
        const float vpH = physicalHeight();

        if (m_scene)
        {
            const Ut::BBox2d bb = m_scene->sceneBBox2D();
            if (bb.isValid())
            {
                m_camera.zoomToBBox(vpW, vpH, bb.minPt.x(), bb.minPt.y(), bb.maxPt.x(), bb.maxPt.y());
                applyView();
                return;
            }
        }
        m_camera.resetToDefault(vpW, vpH);
        applyView();
    }

    bool event(QEvent* event) override
    {
        // macOS 触控板捏合以 NativeGesture 送达（非 Ctrl+Wheel），跨平台统一转缩放
        if (event->type() == QEvent::NativeGesture)
        {
            handleNativeGesture(static_cast<QNativeGestureEvent*>(event));
            return true;
        }
        return RenderWidget::event(event);
    }

protected:
    void wheelEvent(QWheelEvent* event) override
    {
        const float vpW = physicalWidth();
        const float vpH = physicalHeight();
        const QPointF worldPos = m_camera.screenToWorld(physicalPos(event->position()), vpW, vpH);

        const QPoint angleDelta = event->angleDelta();
        const QPointF pixelDelta = event->pixelDelta();
        const Qt::ScrollPhase phase = event->phase();

        switch (classifyWheel(phase, event->modifiers()))
        {
        case TestViewGesture::Zoom:
        {
            float factor;
            // 触控板捏合(Ctrl+滚动阶段)：平滑连续；普通鼠标滚轮(含 Ctrl/Shift)：每格 10%
            if (phase != Qt::NoScrollPhase && event->modifiers().testFlag(Qt::ControlModifier))
            {
                factor = 1.0f + static_cast<float>(angleDelta.y()) / 1200.0f;
            }
            else
            {
                factor = (angleDelta.y() > 0) ? 1.1f : 0.9f;
            }
            m_camera.zoomIn(factor, worldPos, vpW, vpH);
            break;
        }
        case TestViewGesture::Pan:
            // 触控板双指拖动 → 平移视图（方向与鼠标拖拽平移一致）
            m_camera.pan(static_cast<float>(pixelDelta.x()) / m_camera.zoomX,
                -static_cast<float>(pixelDelta.y()) / m_camera.zoomY);
            break;
        case TestViewGesture::HorizontalPan:
        {
            // 触控板 Shift+双指 → 水平平移
            const double scrollValue = (angleDelta.y() != 0)
                ? static_cast<double>(angleDelta.y())
                : (pixelDelta.isNull() ? static_cast<double>(angleDelta.x())
                                       : static_cast<double>(pixelDelta.y()));
            m_camera.pan(static_cast<float>(scrollValue) / m_camera.zoomX, 0.0f);
            break;
        }
        }

        applyView();
        event->accept();
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton)
        {
            m_panning = true;
            m_lastPos = event->pos();
            event->accept();
        }
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (m_panning)
        {
            // 用物理像素增量换算世界位移，保证 Retina/高 DPR 下拖拽跟手
            const float dpr = devicePixelRatio();
            const float worldDx = static_cast<float>(event->pos().x() - m_lastPos.x()) * dpr / m_camera.zoomX;
            const float worldDy = -static_cast<float>(event->pos().y() - m_lastPos.y()) * dpr / m_camera.zoomY;
            m_lastPos = event->pos();
            m_camera.pan(worldDx, worldDy);
            applyView();
        }
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton)
        {
            m_panning = false;
            event->accept();
        }
    }

private:
    void handleNativeGesture(QNativeGestureEvent* event)
    {
        if (event->gestureType() != Qt::ZoomNativeGesture)
        {
            return;
        }
        // macOS 触控板捏合：value() 为原始增量（张开≈+0.05，捏拢≈-0.05）
        const double factor = 1.0 + event->value();
        if (!std::isfinite(factor) || factor < 0.3 || factor > 3.0)
        {
            return;
        }

        const float vpW = physicalWidth();
        const float vpH = physicalHeight();
        const QPointF worldPos = m_camera.screenToWorld(physicalPos(event->position()), vpW, vpH);
        m_camera.zoomIn(static_cast<float>(factor), worldPos, vpW, vpH);
        applyView();
    }

    float physicalWidth() const
    {
        return static_cast<float>(width() * devicePixelRatio());
    }

    float physicalHeight() const
    {
        return static_cast<float>(height() * devicePixelRatio());
    }

    QPoint physicalPos(const QPointF& widgetPos) const
    {
        const float dpr = devicePixelRatio();
        return QPoint(static_cast<int>(widgetPos.x() * dpr), static_cast<int>(widgetPos.y() * dpr));
    }

    void applyView()
    {
        setViewMatrix(m_camera.viewMatrix(physicalWidth(), physicalHeight()));
    }

    Eg::ISceneDataSource* m_scene{ nullptr };
    Camera2D m_camera;
    bool m_panning{ false };
    QPoint m_lastPos;
};

TestViewWindow::TestViewWindow(Eg::ISceneDataSource* scene, QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("Test View"));
    resize(900, 640);

    m_renderWidget = new TestViewRenderWidget(this);
    m_renderWidget->setScene(scene);
    setCentralWidget(m_renderWidget);

    // 等控件显示并完成 OpenGL 初始化后再提交场景几何（内部自带未就绪重试）
    QTimer::singleShot(0, m_renderWidget, &TestViewRenderWidget::finishInit);
}

TestViewWindow::~TestViewWindow() = default;
