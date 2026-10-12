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
    // 按钮由 rebuildButtons 销毁重建，与 m_selectAction 同因：旧按钮 deleteLater 生效前，
    // 本控件仍可能收到 updateHighlight（LanguageChange / 工具切换），裸指针会解引用已删对象。
    QPointer<QToolButton> m_selectButton;
    // 中枢托管，工作台切换（2D→3D）时会先于本控件销毁；用 QPointer 自动置空，
    // 避免本控件在 LanguageChange 等事件里解引用已释放的 QAction。
    QPointer<QAction> m_selectAction;
    // 合成本控件自持（parent=this），不随 rebuildButtons 重建，只建一次
    QAction* m_panAction = nullptr;
};
