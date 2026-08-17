#include "TestViewWindow.h"

#include "RenderWidget.h"
#include "UI/Render/Camera2D.h"
#include "UI/Render/ViewportNavigation2D.h"

#include "Engine/Scene/SceneRenderContract.h"
#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/SyEntity/SyImage.h"
#include "Engine/SyEntity/SyEntity.h"
#include "Engine/SyEntity/EType.h"
#include "Engine/Layer/SyLayer.h"

#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QWheelEvent>

/**
 * @brief 仅显示用渲染控件
 *
 * 继承 RenderWidget 并关闭场景环境(幅面/网格/标尺)，只负责把场景图元画出来。
 * 导航复用共享的 ViewportNavigation2D（与主视口同源）：滚轮缩放、触控板双指
 * 平移/捏合缩放、左/中键拖拽平移；不实现任何选择/编辑逻辑。
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

        // 复用共享导航控制器：手势 → 相机变化的唯一实现，与主视口保持一致
        m_navigation.setRenderWidget(this);
        m_navigation.setCamera(&m_camera);
        m_navigation.setCameraChangedCallback([this]() { applyView(); });
    }

    void setScene(Eg::SceneManager* scene)
    {
        m_scene = scene;
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
            m_navigation.handleNativeGesture(static_cast<QNativeGestureEvent*>(event));
            return true;
        }
        return RenderWidget::event(event);
    }

protected:
    void paintGL() override
    {
        // 首次绘制时（GL 上下文必然 current）再一次性提交场景与位图并适配视图，
        // 避免在 paint 事件之外以非 current 上下文做 GPU 上传导致打开即崩溃。
        if (!m_sceneSubmitted && m_scene && isInitialized())
        {
            m_sceneSubmitted = true;
            submitSceneFromDataSource(m_scene);
            // submitSceneFromDataSource 内部 renderBeginScene 会清空 GPU 位图，
            // 需在此重传所有可见位图纹理（与主视口 reconcileBitmaps 对齐）
            uploadBitmaps();
            fitScene();
        }
        RenderWidget::paintGL();
    }

    void wheelEvent(QWheelEvent* event) override
    {
        // 共享导航控制器统一处理滚轮/触控板手势
        m_navigation.handleWheel(event);
        event->accept();
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton)
        {
            m_panning = true;
            m_navigation.beginPan(physicalPos(event->pos()));
            event->accept();
        }
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (m_panning)
        {
            m_navigation.updatePan(physicalPos(event->pos()));
        }
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton)
        {
            m_panning = false;
            m_navigation.endPan();
            event->accept();
        }
    }

private:
    /// 上传场景中所有可见位图（SyImage）的纹理，使占位符显示为实际图像
    void uploadBitmaps()
    {
        if (!m_scene)
        {
            return;
        }
        for (auto* e : m_scene->getAllEntities())
        {
            if (!e || e->eType != Eg::EType::IMAGE || !e->visible())
            {
                continue;
            }
            if (e->layer() && !e->layer()->isVisible())
            {
                continue;
            }
            const auto* image = static_cast<const Eg::SyImage*>(e);
            if (!image->pixelData() || image->nWidth <= 0 || image->nHeight <= 0)
            {
                continue;
            }
            setBitmapImage(static_cast<uint64_t>(e->id), image);
        }
    }

    float physicalWidth() const
    {
        return static_cast<float>(width() * devicePixelRatio());
    }

    float physicalHeight() const
    {
        return static_cast<float>(height() * devicePixelRatio());
    }

    QPoint physicalPos(const QPoint& widgetPos) const
    {
        const float dpr = devicePixelRatio();
        return QPoint(static_cast<int>(widgetPos.x() * dpr), static_cast<int>(widgetPos.y() * dpr));
    }

    void applyView()
    {
        setViewMatrix(m_camera.viewMatrix(physicalWidth(), physicalHeight()));
    }

    Eg::SceneManager* m_scene{ nullptr };
    Camera2D m_camera;
    ViewportNavigation2D m_navigation;
    bool m_panning{ false };
    bool m_sceneSubmitted{ false };
};

TestViewWindow::TestViewWindow(Eg::SceneManager* scene, QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("Test View"));
    resize(900, 640);

    m_renderWidget = new TestViewRenderWidget(this);
    m_renderWidget->setScene(scene);
    setCentralWidget(m_renderWidget);
}

TestViewWindow::~TestViewWindow() = default;

