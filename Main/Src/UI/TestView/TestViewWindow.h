#pragma once

#include <QMainWindow>

namespace Eg
{
    class ISceneDataSource;
}

class TestViewRenderWidget;

/**
 * @brief 独立的“仅显示”渲染窗口（TestView 菜单）
 *
 * 打开后以独立窗口展示当前 2D 场景的全部图元，仅支持缩放/平移，
 * 不含工作台(幅面)、网格、标尺等场景环境，也不参与任何选择/编辑。
 */
class TestViewWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit TestViewWindow(Eg::ISceneDataSource* scene, QWidget* parent = nullptr);
    ~TestViewWindow() override;

private:
    TestViewRenderWidget* m_renderWidget{ nullptr };
};
