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
        connect(m_jobService, &ProcessingJobService::jobStarted, this, &DevicePanel::refreshState);
        connect(m_jobService, &ProcessingJobService::jobFinished, this, &DevicePanel::refreshState);
        connect(m_jobService, &ProcessingJobService::jobStateChanged, this, &DevicePanel::refreshState);
    }

    if (m_safety)
    {
        connect(m_safety, &EmergencyStopController::stateChanged, this, &DevicePanel::refreshState);
    }
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
