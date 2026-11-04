#pragma once

/**
 * @file DevicePanel.h
 * @brief 设备面板：展示设备与安全状态，并提供加工启停入口。
 *
 * 硬件模块未编入时面板依然显示，保持布局不变，但全部控件禁用并给出提示。
 * 本面板不含安全功能，急停由独立常驻入口承担。
 */

#include <QWidget>

class QLabel;
class QPushButton;
class DeviceHost;
class ProcessingJobService;
class EmergencyStopController;
class OperationBus;

class DevicePanel : public QWidget
{
    Q_OBJECT

public:
    explicit DevicePanel(QWidget* parent = nullptr);

    /// 注入依赖，便于测试与替换；传 nullptr 表示对应能力不可用。
    DevicePanel(DeviceHost* host, ProcessingJobService* jobService, OperationBus* bus,
        EmergencyStopController* safety, QWidget* parent = nullptr);

    /// 供测试直接刷新状态。
    void refreshState();

private:
    void buildUi();
    void connectSignals();
    void runStart();
    void runPauseResume();
    void runStop();

    DeviceHost* m_host{ nullptr };
    ProcessingJobService* m_jobService{ nullptr };
    OperationBus* m_bus{ nullptr };
    EmergencyStopController* m_safety{ nullptr };

    QLabel* m_statusLabel{ nullptr };
    QLabel* m_safetyLabel{ nullptr };
    QLabel* m_hintLabel{ nullptr };
    QPushButton* m_startButton{ nullptr };
    QPushButton* m_pauseButton{ nullptr };
    QPushButton* m_stopButton{ nullptr };
};
