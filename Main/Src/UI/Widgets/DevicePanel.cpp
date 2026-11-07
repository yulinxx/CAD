#include "UI/Widgets/DevicePanel.h"

#include <QCoreApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "Composition/ApplicationCompositionRoot.h"
#include "Hardware/DeviceHost.h"
#include "Hardware/ProcessingJobService.h"
#include "UI/Command/OperationId.h"
#include "UI/Safety/EmergencyStopController.h"
#include "UI2D/Operation/OperationBus.h"
#include "Log/SyLogger.h"

namespace
{
QString tr(const char* text)
{
    return QCoreApplication::translate("DevicePanel", text);
}
}  // namespace

DevicePanel::DevicePanel(QWidget* parent)
    : QWidget(parent)
{
    buildUi();

    auto* root = ApplicationCompositionRoot::instance();
    if (root)
    {
        m_host = root->deviceHost();
        m_jobService = root->processingJobService();
        m_bus = root->operationBus();
        m_safety = root->emergencyStopController();
    }

    connectSignals();
    refreshState();
}

DevicePanel::DevicePanel(DeviceHost* host, ProcessingJobService* jobService, OperationBus* bus,
    EmergencyStopController* safety, QWidget* parent)
    : QWidget(parent)
    , m_host(host)
    , m_jobService(jobService)
    , m_bus(bus)
    , m_safety(safety)
{
    buildUi();
    connectSignals();
    refreshState();
}

void DevicePanel::buildUi()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName(QStringLiteral("devicePanelStatus"));
    m_statusLabel->setWordWrap(true);
    layout->addWidget(m_statusLabel);

    m_safetyLabel = new QLabel(this);
    m_safetyLabel->setObjectName(QStringLiteral("devicePanelSafety"));
    m_safetyLabel->setWordWrap(true);
    layout->addWidget(m_safetyLabel);

    // 加工告警常驻位：默认隐藏，只在 jobWarning / jobFinished(false) 时出现。
    // 不用 QMessageBox 是为了不打断正在进行的加工，也不会在自动化里弹模态框。
    m_warningLabel = new QLabel(this);
    m_warningLabel->setObjectName(QStringLiteral("devicePanelWarning"));
    m_warningLabel->setWordWrap(true);
    m_warningLabel->setVisible(false);
    m_warningLabel->setStyleSheet(QStringLiteral("QLabel { font-weight: bold; }"));
    layout->addWidget(m_warningLabel);

    m_hintLabel = new QLabel(tr("Hardware module is not enabled in this build (BUILD_HARDWARE=OFF). "
                                "The panel is shown for reference only."), this);
    m_hintLabel->setObjectName(QStringLiteral("devicePanelHint"));
    m_hintLabel->setWordWrap(true);
    layout->addWidget(m_hintLabel);

    auto* buttonRow = new QHBoxLayout();
    buttonRow->setSpacing(6);

    m_startButton = new QPushButton(tr("Start Processing"), this);
    m_startButton->setObjectName(QStringLiteral("devicePanelStart"));
    m_pauseButton = new QPushButton(tr("Pause"), this);
    m_pauseButton->setObjectName(QStringLiteral("devicePanelPause"));
    m_stopButton = new QPushButton(tr("Stop"), this);
    m_stopButton->setObjectName(QStringLiteral("devicePanelStop"));

    buttonRow->addWidget(m_startButton);
    buttonRow->addWidget(m_pauseButton);
    buttonRow->addWidget(m_stopButton);
    layout->addLayout(buttonRow);
    layout->addStretch(1);

    connect(m_startButton, &QPushButton::clicked, this, &DevicePanel::runStart);
    connect(m_pauseButton, &QPushButton::clicked, this, &DevicePanel::runPauseResume);
    connect(m_stopButton, &QPushButton::clicked, this, &DevicePanel::runStop);
}

void DevicePanel::connectSignals()
{
    if (m_host)
    {
        connect(m_host, &DeviceHost::deviceStateChanged, this, &DevicePanel::refreshState);
        connect(m_host, &DeviceHost::machineStateChanged, this, &DevicePanel::refreshState);
        connect(m_host, &DeviceHost::safetyVerdictChanged, this, &DevicePanel::refreshState);
    }

    if (m_jobService)
    {
        connect(m_jobService, &ProcessingJobService::jobStarted, this, &DevicePanel::clearJobWarning);
        connect(m_jobService, &ProcessingJobService::jobStarted, this, &DevicePanel::refreshState);
        connect(m_jobService, &ProcessingJobService::jobFinished, this, &DevicePanel::onJobFinished);
        connect(m_jobService, &ProcessingJobService::jobFinished, this, &DevicePanel::refreshState);
        connect(m_jobService, &ProcessingJobService::jobStateChanged, this, &DevicePanel::refreshState);
        // 生产代码里唯一一条把 jobWarning 送到操作员眼前的连线。
        // ProcessingJobService 在 LaserRoute 报告「只有首段激光参数被落实」时发此信号，
        // 之前全仓只有单测监听它，现场会把它当成一次成功的加工。
        connect(m_jobService, &ProcessingJobService::jobWarning, this, &DevicePanel::onJobWarning);
    }

    if (m_safety)
    {
        connect(m_safety, &EmergencyStopController::stateChanged, this, &DevicePanel::refreshState);
    }
}

void DevicePanel::clearJobWarning()
{
    if (m_warningLabel)
    {
        m_warningLabel->clear();
        m_warningLabel->setVisible(false);
    }
}

void DevicePanel::onJobWarning(const QString& message)
{
    if (message.isEmpty())
    {
        return;
    }
    SY_WARNF("[DevicePanel] Job warning: %s", message.toUtf8().constData());
    if (!m_warningLabel)
    {
        return;
    }
    m_warningLabel->setText(tr("Warning: %1").arg(message));
    m_warningLabel->setVisible(true);
}

void DevicePanel::onJobFinished(bool success, const QString& message)
{
    if (success)
    {
        return;
    }
    SY_ERRORF("[DevicePanel] Job failed: %s", message.toUtf8().constData());
    if (!m_warningLabel)
    {
        return;
    }
    m_warningLabel->setText(message.isEmpty()
        ? tr("Processing failed")
        : tr("Processing failed: %1").arg(message));
    m_warningLabel->setVisible(true);
}

void DevicePanel::refreshState()
{
    const bool hardwareCompiled = DeviceHost::isHardwareSupportCompiled();
    const bool running = m_host && m_host->isRunning();
    const bool readOnly = m_safety && m_safety->isReadOnly();

    if (!m_statusLabel)
    {
        return;
    }

    if (!hardwareCompiled)
    {
        m_statusLabel->setText(tr("Hardware module not enabled"));
    }
    else if (!running)
    {
        m_statusLabel->setText(tr("Device not started"));
    }
    else
    {
        QString name = m_host->deviceDisplayName();
        if (name.isEmpty())
        {
            name = m_host->deviceId();
        }
        if (m_host->isSimulated())
        {
            name += tr(" (Simulated)");
        }
        m_statusLabel->setText(tr("Device: %1").arg(name));
    }

    if (!hardwareCompiled)
    {
        m_safetyLabel->setText(tr("Safety monitoring unavailable"));
    }
    else
    {
        const QStringList violations = m_host ? m_host->safetyViolations() : QStringList();
        m_safetyLabel->setText(violations.isEmpty() ? tr("Safety inputs clear") : violations.first());
    }

    m_hintLabel->setVisible(!hardwareCompiled);

    const bool jobRunning = m_jobService && m_jobService->isRunning();
    const bool jobPaused = m_jobService && m_jobService->isPaused();
    const bool jobActive = jobRunning || jobPaused;
    const bool controlsAvailable = hardwareCompiled && running && !readOnly;
    m_startButton->setEnabled(controlsAvailable && !jobActive);
    m_pauseButton->setEnabled(controlsAvailable && jobActive);
    m_stopButton->setEnabled(controlsAvailable && jobActive);

    m_pauseButton->setText(jobPaused ? tr("Resume") : tr("Pause"));
}

void DevicePanel::runStart()
{
    if (m_bus)
    {
        m_bus->run(Cmd::OperationId::Laser_StartProcess);
    }
}

void DevicePanel::runPauseResume()
{
    // 暂停与恢复由同一个命令按作业状态切换，面板只负责把意图发给中枢
    if (m_bus)
    {
        m_bus->run(Cmd::OperationId::Laser_PauseProcess);
    }
}

void DevicePanel::runStop()
{
    if (m_bus)
    {
        m_bus->run(Cmd::OperationId::Laser_StopProcess);
    }
}
