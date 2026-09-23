#pragma once

#include <QWidget>
#include <QVector>
#include <QString>
#include <QIcon>
#include <QAction>
#include <functional>

class QToolButton;

/**
 * @brief 左侧绘图工具面板（纯展示层）
 *
 * 将命令中枢托管的 QAction 展示为一列按钮。Select/Pan 工具按钮可相互切换。
 *
 * 高亮逻辑：
 * - 启动默认 Select 高亮
 * - 点击 Select/Pan 按钮时在 Select 和 Pan 之间切换，保持高亮
 * - 点击其他工具时，其他工具高亮，Select/Pan 取消高亮
 * - 按 ESC 回到 Select/Pan 状态并高亮
 */
class DrawToolBarWidget : public QWidget
{
    Q_OBJECT

public:
    explicit DrawToolBarWidget(QWidget* parent = nullptr);
    ~DrawToolBarWidget() override;

public:
    void setToolActions(const QVector<QAction*>& actions);

    using PanModeCallback = std::function<bool()>;
    void setPanModeToggleCallback(PanModeCallback callback);

    using IsPanModeCallback = std::function<bool()>;
    void setIsPanModeCallback(IsPanModeCallback callback);

    /// 设置当前活动工具名称
    void setCurrentToolName(const QString& toolName);

    /// 设置 Pan 模式状态
    void setPanMode(bool enabled);

    /// 更新按钮高亮状态（Pan 模式变化时调用）
    void updateHighlight();

signals:
    void iconNeedsUpdate();

private:
    void rebuildButtons();
    void updateSelectButtonAction();

    QVector<QAction*> m_toolActions;
    QString m_currentToolName;
    bool m_isPanMode = false;

    PanModeCallback m_panModeToggleCallback;
    IsPanModeCallback m_isPanModeCallback;
    QToolButton* m_selectButton = nullptr;
    QAction* m_selectAction = nullptr;
    QAction* m_panAction = nullptr;
};
