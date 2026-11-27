#pragma once

#include <QHash>
#include <QSet>
#include <QString>
#include <QWidget>

class QVBoxLayout;
class QScrollArea;

namespace MS
{
    // 右侧硬件/加工面板内容容器：构造期一次性创建四个子面板并 add 到同一滚动区；
    // 工具栏按钮点击 → showPanel/hidePanel 独立控制每个子面板的显隐；
    // 多个面板可以同时可见（累加显示），与源项目 RightToolWidget 的布局行为一致。
    //
    //   property  → 属性（图层/图元/几何信息）
    //   coordinate → 机床坐标显示 + 工件原点
    //   manual    → 手动点动控制面板（XYZ 移动 + 激光开关 + 回原点）
    //   process   → 加工控制面板（进度/速度/Start-Pause-Stop）
    class RightToolWidget : public QWidget
    {
        Q_OBJECT

    public:
        explicit RightToolWidget(QWidget* parent = nullptr);
        ~RightToolWidget() override = default;

    public slots:
        // 切换：已显示 → 隐藏，已隐藏 → 显示。不合法的 panelId 忽略。
        void togglePanel(const QString& panelId);
        // 确保指定面板显示（已显示则不动）。
        void showPanel(const QString& panelId);
        // 确保指定面板隐藏（已隐藏则不动）。
        void hidePanel(const QString& panelId);
        // 一次性隐藏所有面板。
        void clearAllPanels();

    public:
        bool isPanelVisible(const QString& panelId) const;
        QSet<QString> visiblePanelIds() const;
        bool hasAnyVisible() const;

    signals:
        void sigPanelShown(const QString& panelId);
        void sigPanelHidden(const QString& panelId);

    private:
        QWidget* initPropertyPanel();
        QWidget* initCoordinatePanel();
        QWidget* initManualPanel();
        QWidget* initProcessPanel();

        void applyPanelVisible(const QString& panelId, bool visible);

    private:
        QScrollArea* m_scroll = nullptr;             // 内容滚动容器
        QVBoxLayout* m_contentLayout = nullptr;       // 四个面板所在的布局
        QHash<QString, QWidget*> m_panels;            // panelId → 子面板 widget（构造期全部建好）

        // panelId → 目标显隐状态（true = 应该显示）。
        // **必须用独立状态表**，不能用 QWidget::isVisible()：
        // Qt::isVisible() 在父 widget hidden 时会强制返回 false，
        // 导致 updatePanelVisibility / togglePanel 永远误判。
        QHash<QString, bool> m_panelStates;
    };
}  // namespace MS
