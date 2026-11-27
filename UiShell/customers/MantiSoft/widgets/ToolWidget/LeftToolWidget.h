#pragma once

#include <QWidget>

class QVBoxLayout;
class QScrollArea;

namespace MS
{
    // 左侧侧边栏（展开后的工具参数面板容器）：对应源项目 Widget/LeftToolWidget。
    //   - 常驻隐藏；由 LeftSidePanel 的 LeftToolBar 按钮经 set_content() 注入内容后展开；
    //   - 内容放进同一个可滚动容器（滚动方向垂直），后 set 的覆盖前一个；
    //   - 面板最小宽度 s_nMinWidth（默认 340），超出内容宽度时随内容撑开。
    // 只做 UI 骨架（容器/滚动/宽度），不持有任何业务状态。
    class LeftToolWidget : public QWidget
    {
        Q_OBJECT

    public:
        explicit LeftToolWidget(QWidget* parent = nullptr);
        ~LeftToolWidget() override = default;

        // 设置当前显示的内容控件；传入 nullptr 时行为与 remove_all_content() 相同。
        void set_content(QWidget* w, int width = s_nMinWidth);
        // 卸载指定控件；若它是当前内容，则隐藏本面板。
        void remove_content(QWidget* w);
        // 卸载全部内容并隐藏本面板。
        void remove_all_content();

        QWidget* current_content() const;

        // 回退到上一次的内容（上一次 set_content / remove_all_content 前的 curWidget）。
        void setPreWidget(QWidget* widget);

    signals:
        // content 非空 -> true；清空 -> false。
        void sigContentChanged(bool bHasContent);

    public:
        static constexpr int s_nMinWidth = 340;

    private:
        QVBoxLayout* m_contentLayout = nullptr;   // 内容区布局（放入滚动容器内部）
        QVBoxLayout* m_scrolLayout = nullptr;     // 滚动容器布局
        QScrollArea* m_scroll = nullptr;
        QWidget* m_curWidget = nullptr;
        QWidget* m_preWidget = nullptr;
    };
}  // namespace MS
