#pragma once

#include <QWidget>
#include <QVector>
#include <QString>
#include <QIcon>
#include <QAction>
#include <QPointer>
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

protected:
    /// 语言切换时重译 Select/Pan 悬停提示（本类不是 ToolBarBase，需自行处理 LanguageChange）
    void changeEvent(QEvent* event) override;

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
    // 借用的中枢 QAction（parent 是主窗口，生命周期归 CommandActionHub）。
    // 枢纽 reset() 时会 deleteLater 这些动作，切换工作台的
    // sendPostedEvents(DeferredDelete) 随即把它们真正释放——
    // 而本 widget 若未随 Dock 销毁（host=ToolBar 时只挂在主窗口上），
    // 仍会收到 LanguageChange 并走进 updateHighlight。
    // 必须用 QPointer 跟踪，悬空时自动置空、由 updateHighlight 的空判保护。
    QPointer<QAction> m_selectAction;
    QAction* m_panAction = nullptr;
};
