#include "TestViewWindow.h"

#include "RenderWidget.h"
#include "UI/Render/Camera2D.h"
#include "UI/Render/ViewportNavigation2D.h"

#include "Engine/Scene/SceneRenderContract.h"
#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/Core/SceneNotifier.h"
#include "Engine2D/SyEntity/SyImage.h"
#include "Engine/SyEntity/SyEntity.h"
#include "Engine/SyEntity/EType.h"
#include "Engine/Layer/SyLayer.h"

#include <QCheckBox>
#include <QEvent>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QVBoxLayout>
#include <QWheelEvent>

/**
 * @brief 仅显示用渲染控件
 *
 * 继承 RenderWidget 并关闭场景环境(幅面/网格/标尺)，只负责把场景图元画出来。
 * 导航复用共享的 ViewportNavigation2D（与主视口同源）：滚轮缩放、触控板双指
 * 平移/捏合缩放、左/中键拖拽平移；不实现任何选择/编辑逻辑。
 *
 * liveSync = true 时订阅场景变化，在变更后重提图元/位图实现实时同步；
 * liveSync = false 时为打开时的静态快照。
 */
class TestViewRenderWidget : public RenderWidget
{
public:
    explicit TestViewRenderWidget(bool liveSync, QWidget* parent = nullptr)
        : RenderWidget(parent)
        , m_liveSync(liveSync)
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

    ~TestViewRenderWidget() override
    {
        detachObserver();
    }

    void setScene(Eg::SceneManager* scene)
    {
        // 场景即将替换：先解绑旧观察者再绑新
        detachObserver();
        m_scene = scene;
        if (m_liveSync)
        {
            attachObserver();
        }
    }

    void setLiveSync(bool on)
    {
        if (on == m_liveSync)
        {
            return;
        }
        m_liveSync = on;
        if (on)
        {
            attachObserver();
        }
        else
        {
            detachObserver();
        }
        // 开/关即时生效：关闭后回归快照（冻结当前渲染），打开后立刻追上最新
        markDirtyAndRefresh();
    }

    bool liveSync() const
    {
        return m_liveSync;
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
        // 在 GL 上下文 current 的第一帧（及每次 dirty）时提交场景与位图，
        // 避免在 paint 之外以非 current 上下文做 GPU 上传导致崩溃。
        if (m_scene && isInitialized())
        {
            const bool firstTime = !m_sceneSubmitted;
            if (firstTime && m_liveSync)
            {
                attachObserver();
            }
            if (firstTime || (m_liveSync && m_sceneDirty))
            {
                submitSceneFromDataSource(m_scene);
                uploadBitmaps();
                if (firstTime)
                {
                    fitScene();
                }
                m_sceneSubmitted = true;
                m_sceneDirty = false;
            }
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
    void onSceneChanged()
    {
        if (!m_liveSync)
        {
            return;
        }
        markDirtyAndRefresh();
    }

    void markDirtyAndRefresh()
    {
        if (isInitialized())
        {
            m_sceneDirty = true;
            update();
        }
    }

    void attachObserver()
    {
        if (m_scene && !m_observerAttached)
        {
            m_scene->addObserver(&m_observer);
            m_observerAttached = true;
        }
    }

    void detachObserver()
    {
        if (m_scene && m_observerAttached)
        {
            m_scene->removeObserver(&m_observer);
            m_observerAttached = false;
        }
    }

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

    /// 场景变更观察者，转发到宿主控件
    class TestViewSceneObserver : public Eg::SceneNotifier::IObserver
    {
    public:
        explicit TestViewSceneObserver(TestViewRenderWidget* self)
            : m_self(self)
        {
        }
        void onSceneChanged() override
        {
            if (m_self)
            {
                m_self->onSceneChanged();
            }
        }

        void onEntityAdded(Eg::SyEntity* /*entity*/) override
        {
            if (m_self)
            {
                m_self->onSceneChanged();
            }
        }

        void onEntityRemoved(size_t /*index*/) override
        {
            if (m_self)
            {
                m_self->onSceneChanged();
            }
        }

    private:
        TestViewRenderWidget* m_self;
    };

    Eg::SceneManager* m_scene{ nullptr };
    Camera2D m_camera;
    ViewportNavigation2D m_navigation;
    TestViewSceneObserver m_observer{ this };
    bool m_observerAttached{ false };
    bool m_liveSync{ false };
    bool m_sceneSubmitted{ false };
    bool m_sceneDirty{ false };
    bool m_panning{ false };
};

TestViewWindow::TestViewWindow(Eg::SceneManager* scene, bool liveSync, QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(liveSync ? tr("Test View (Live)") : tr("Test View"));
    resize(900, 640);

    auto* container = new QWidget(this);
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(2, 2, 2, 2);

    auto* syncBox = new QCheckBox(tr("Sync with scene (live)"), container);
    syncBox->setToolTip(tr("实时同步场景变化（关闭时保持当前快照）"));
    syncBox->setChecked(liveSync);
    layout->addWidget(syncBox);

    m_renderWidget = new TestViewRenderWidget(liveSync, this);
    m_renderWidget->setScene(scene);
    layout->addWidget(m_renderWidget);

    setCentralWidget(container);

    connect(syncBox, &QCheckBox::toggled, m_renderWidget, &TestViewRenderWidget::setLiveSync);
}

TestViewWindow::~TestViewWindow() = default;
