#pragma once

#include <QWidget>
#include <QVector>
#include <QString>
#include <functional>

class QAction;
class QToolButton;

/**
 * @brief 左侧绘图工具面板（纯展示层）
 *
 * 只负责把命令中枢（CommandActionHub）托管的 QAction 摆成一列按钮：
 * 图标、文案、启用态、勾选态全部由 QAction 承载，点击直接 trigger 该 QAction，
 * 因此绘图工具的派发路径与菜单/右键菜单完全一致，本类不再持有 OperationBus，
 *也不再自己解析 toolName → OperationId。
 *
 * 承载样式无关：无论放进 QToolBar 还是 QDockWidget，复用同一份内容控件。
 *
 * 特殊处理：点击 Select 工具按钮时，如果当前是 Select 则切换到 Pan 模式，
 * 如果当前是 Pan 模式则切换到 Select 工具。
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

    /// 设置 Pan 模式切换回调（用于 Select 和 Pan 之间的切换）
    using PanModeCallback = std::function<bool()>;
    void setPanModeToggleCallback(PanModeCallback callback);
    /// 设置获取当前 Pan 模式的回调
    using IsPanModeCallback = std::function<bool()>;
    void setIsPanModeCallback(IsPanModeCallback callback);

    /// 设置当前活动工具名称（用于 Select/Pan toggle 逻辑）
    void setCurrentToolName(const QString& toolName);

private:
    void rebuildButtons();

    QVector<QAction*> m_toolActions;
    // 当前活动工具名称
    QString m_currentToolName;
    // Pan 模式切换回调
    PanModeCallback m_panModeToggleCallback;
    // 获取当前是否处于 Pan 模式的回调
    IsPanModeCallback m_isPanModeCallback;
};
