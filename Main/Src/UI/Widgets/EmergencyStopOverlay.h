#pragma once

/**
 * @file EmergencyStopOverlay.h
 * @brief 常驻急停入口：固定位置、红色、单击即触发，不弹二次确认。
 *
 * 采用独立置顶窗口而不是停靠面板，保证切换工作台、隐藏面板或打开窗口内对话框时
 * 急停入口仍然可见。窗口本身只做请求与状态展示，安全功能由硬回路承担。
 */

#include <functional>

#include <QWidget>

class QLabel;
class QPushButton;
class EmergencyStopController;

class EmergencyStopOverlay : public QWidget
{
    Q_OBJECT

public:
    EmergencyStopOverlay(EmergencyStopController* controller, std::function<void()> emergencyRequest,
        std::function<void()> startRequest, QWidget* parent = nullptr);

    /// 绑定主窗口：跟随主窗口移动/缩放，主窗口显示时一并显示。
    void attachToWindow(QWidget* window);

    QPushButton* emergencyButton() const;
    QPushButton* confirmResetButton() const;
    QPushButton* startLaserButton() const;
    QLabel* stepsLabel() const;
    QLabel* safetyNoticeLabel() const;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void buildUi();
    void updateUi();
    /// 按急停状态决定是否显示：未触发急停时完全不存在，不做半透明常驻控件
    void syncVisibility();
    void reposition();

    EmergencyStopController* m_controller{ nullptr };
    std::function<void()> m_emergencyRequest;
    std::function<void()> m_startRequest;
    QWidget* m_window{ nullptr };

    QPushButton* m_emergencyButton{ nullptr };
    QLabel* m_statusLabel{ nullptr };
    QLabel* m_stepsLabel{ nullptr };
    QLabel* m_resetStateLabel{ nullptr };
    QPushButton* m_confirmResetButton{ nullptr };
    QPushButton* m_startLaserButton{ nullptr };
    QLabel* m_safetyNoticeLabel{ nullptr };
};
