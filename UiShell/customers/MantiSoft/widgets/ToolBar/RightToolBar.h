#pragma once

#include <QList>
#include <QWidget>

class QFrame;
class QToolButton;
class QVBoxLayout;

namespace MS
{
    // 右侧工具栏：窄竖条（~40px），常驻显示。
    // 结构对齐源项目 RightToolBar：
    //   顶部分隔线
    //   + 面板切换（Property / Coordinate / Manual / Work control）—— 独立 Toggle，互不排斥
    //   + Stretch
    //   + 分隔线
    //   + 次级工具区（Group / Mirror / Alignment / Same Spacing / Move Shapes To / Zoom / Measurement）
    //   + Contract/Expand 按钮（折叠次级工具区）
    //   + 底部固定留白
    class RightToolBar : public QWidget
    {
        Q_OBJECT

    public:
        explicit RightToolBar(QWidget* parent = nullptr);
        ~RightToolBar() override = default;

        // 顶部四个面板切换按钮（Property / Coordinate / Manual / Process）的只读访问。
        // 用于外部根据按钮 checked 状态控制面板显隐、或反向同步按钮状态。
        const QList<QToolButton*>& toolButtons() const
        {
            return m_toolButtons;
        }

    public slots:
        // 将指定 panelId 的按钮设为 checked，其余全部取消。
        // panelId 为空 → 全部取消（全部 unchecked）。
        void setCurrentPanel(const QString& panelId);

        // 单独设置某个面板按钮的 checked 状态（由 RightToolWidget 的 show/hide 信号反向同步时使用）。
        void setPanelActive(const QString& panelId, bool active);

    signals:
        // 面板切换按钮点击；panelId 取值："property" / "coordinate" / "manual" / "process"
        // 按钮本身是 checkable，且没有 autoExclusive → 多个可以同时 checked（累加显示）。
        // 发出此信号时按钮的 checked 状态已经翻转（Qt checkable 按钮默认行为），
        // 接收方可以直接用 btn->isChecked() 决定开/关面板。
        void sigPanelButtonClicked(const QString& panelId);

        // 次级工具区（菜单项 / Measurement）点击；commandId 与 Commands 登记的命令 id 一致。
        void sigCommandTriggered(const QString& commandId);

    private slots:
        void onSecondaryToggle();

    private:
        void buildButtons(QVBoxLayout* lay);
        void buildSecondaryTools(QVBoxLayout* lay);

        QToolButton* createToolButton(const QString& panelId, const QString& tooltip);
        QToolButton* addToolButton(QVBoxLayout* lay, const QString& panelId,
            const QString& tooltip, const char* iconAlias);

    private:
        QList<QToolButton*> m_toolButtons;   // 顶部四个面板切换按钮
        QWidget* m_secondarySection = nullptr;
        QFrame* m_secondaryLine = nullptr;   // 次级工具区上方的分隔线，随折叠一起显隐
        QToolButton* m_expandBtn = nullptr;
        bool m_bSecondaryExpanded = true;
    };
}  // namespace MS
