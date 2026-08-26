#pragma once

#include <QWidget>
#include <QVector>

class QAction;
class QToolButton;

/**
 * @brief 左侧绘图工具面板（纯展示层）
 *
 * 只负责把命令中枢（CommandActionHub）托管的 QAction 摆成一列按钮：
 * 图标、文案、启用态、勾选态全部由 QAction 承载，点击直接 trigger 该 QAction，
 * 因此绘图工具的派发路径与菜单/右键菜单完全一致，本类不再持有 OperationBus，
 * 也不再自己解析 toolName → OperationId。
 *
 * 承载样式无关：无论放进 QToolBar 还是 QDockWidget，复用同一份内容控件。
 */
class DrawToolBarWidget : public QWidget
{
    Q_OBJECT

public:
    explicit DrawToolBarWidget(QWidget* parent = nullptr);
    ~DrawToolBarWidget() override;

public:
    /**
     * @brief 用命令中枢的 QAction 填充按钮列
     *
     * @param actions 已按目录顺序排好的工具动作；nullptr 项会被跳过
     */
    void setToolActions(const QVector<QAction*>& actions);

private:
    void rebuildButtons();

    QVector<QAction*> m_toolActions;
};
