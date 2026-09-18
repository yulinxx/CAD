#pragma once

#include <QWidget>
#include <QVector>
#include <QString>
#include <QIcon>
#include <functional>

class QAction;
class QToolButton;

/**
 * @brief 左侧绘图工具面板（纯展示层）
 *
 * 将命令中枢托管的 QAction 展示为一列按钮。Select/Pan 工具按钮可相互切换。
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

    /// 更新 Select 按钮图标（根据当前是 Select 还是 Pan 模式）
    void updateSelectButtonIcon();

signals:
    /// 图标需要更新时发出
    void iconNeedsUpdate();

private:
    void rebuildButtons();

    QVector<QAction*> m_toolActions;
    // 当前活动工具名称
    QString m_currentToolName;
    // Pan 模式切换回调
    PanModeCallback m_panModeToggleCallback;
    // 获取当前是否处于 Pan 模式的回调
    IsPanModeCallback m_isPanModeCallback;
    // Select 按钮指针（用于更新图标）
    QToolButton* m_selectButton = nullptr;
};
