#pragma once

#include <QMainWindow>

namespace Eg
{
    class SceneManager;
}

class TestViewRenderWidget;

/**
 * @brief 独立的“仅显示”渲染窗口（TestView 菜单）
 *
 * 打开后以独立窗口展示当前 2D 场景的全部图元（含位图），仅支持缩放/平移。
 * 关闭工作台(幅面)、网格、标尺等场景环境，且不参与选择/编辑。
 *
 * liveSync 控制两种模式：
 *   - false（默认）：打开时的静态快照；
 *   - true：订阅场景变化，实时重提图元/位图，保持与主视图同步。
 * 通过窗口内的 Checkbox 可以动态切换。
 */
class TestViewWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit TestViewWindow(Eg::SceneManager* scene, bool liveSync = false, QWidget* parent = nullptr);
    ~TestViewWindow() override;

private:
    TestViewRenderWidget* m_renderWidget{ nullptr };
};
