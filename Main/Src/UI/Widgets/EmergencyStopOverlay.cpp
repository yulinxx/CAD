#include "UI/Widgets/EmergencyStopOverlay.h"

#include <QCoreApplication>
#include <QEvent>
#include <QGuiApplication>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>

#include "Log/AprLogger.h"
#include "UI/Safety/EmergencyStopController.h"

namespace
{
QString overlayTr(const char* text)
{
    return QCoreApplication::translate("EmergencyStopOverlay", text);
}
}  // namespace

EmergencyStopOverlay::EmergencyStopOverlay(EmergencyStopController* controller,
    std::function<void()> emergencyRequest, std::function<void()> startRequest, QWidget* parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus)
    , m_controller(controller)
    , m_emergencyRequest(std::move(emergencyRequest))
    , m_startRequest(std::move(startRequest))
{
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setAttribute(Qt::WA_StyledBackground, true);
    setObjectName(QStringLiteral("emergencyStopOverlay"));
    setStyleSheet(QStringLiteral("EmergencyStopOverlay { background-color: rgba(255, 255, 255, 235); "
                                 "border: 1px solid #c62828; border-radius: 6px; }"));
    buildUi();

    if (m_controller)
    {
        connect(m_controller, &EmergencyStopController::stateChanged, this, [this]() { updateUi(); });
        connect(m_controller, &EmergencyStopController::physicalResetObservedChanged, this, [this]() { updateUi(); });
    }

    updateUi();
}

QPushButton* EmergencyStopOverlay::emergencyButton() const
{
    return m_emergencyButton;
}

QPushButton* EmergencyStopOverlay::confirmResetButton() const
{
    return m_confirmResetButton;
}

QPushButton* EmergencyStopOverlay::startLaserButton() const
{
    return m_startLaserButton;
}

QLabel* EmergencyStopOverlay::stepsLabel() const
{
    return m_stepsLabel;
}

QLabel* EmergencyStopOverlay::safetyNoticeLabel() const
{
    return m_safetyNoticeLabel;
}

void EmergencyStopOverlay::buildUi()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(6);

    m_emergencyButton = new QPushButton(overlayTr("EMERGENCY STOP"), this);
    m_emergencyButton->setObjectName(QStringLiteral("emergencyStopButton"));
    m_emergencyButton->setMinimumHeight(44);
    m_emergencyButton->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: #c62828; color: white; font-weight: bold; border: none; "
        "border-radius: 4px; padding: 8px 14px; } "
        "QPushButton:pressed { background-color: #8e0000; }"));
    layout->addWidget(m_emergencyButton);

    m_safetyNoticeLabel = new QLabel(
        overlayTr("Safety functions are provided by the hard-wired safety circuit; this software only monitors "
           "and displays status."),
        this);
    m_safetyNoticeLabel->setObjectName(QStringLiteral("emergencySafetyNotice"));
    m_safetyNoticeLabel->setWordWrap(true);
    layout->addWidget(m_safetyNoticeLabel);

    m_statusLabel = new QLabel(overlayTr("EMERGENCY STOP ACTIVE"), this);
    m_statusLabel->setObjectName(QStringLiteral("emergencyStatus"));
    m_statusLabel->setStyleSheet(QStringLiteral("color: #c62828; font-weight: bold;"));
    layout->addWidget(m_statusLabel);

    m_stepsLabel = new QLabel(
        overlayTr("1. Remove obstructions and make sure personnel are clear.\n"
           "2. Physically reset the emergency stop (release or rotate the hardware button).\n"
           "3. Click Confirm Reset."),
        this);
    m_stepsLabel->setObjectName(QStringLiteral("emergencySteps"));
    m_stepsLabel->setWordWrap(true);
    layout->addWidget(m_stepsLabel);

    m_resetStateLabel = new QLabel(this);
    m_resetStateLabel->setObjectName(QStringLiteral("emergencyResetState"));
    m_resetStateLabel->setWordWrap(true);
    layout->addWidget(m_resetStateLabel);

    m_confirmResetButton = new QPushButton(overlayTr("Confirm Reset"), this);
    m_confirmResetButton->setObjectName(QStringLiteral("emergencyConfirmReset"));
    layout->addWidget(m_confirmResetButton);

    m_startLaserButton = new QPushButton(overlayTr("Start Processing"), this);
    m_startLaserButton->setObjectName(QStringLiteral("emergencyStartLaser"));
    layout->addWidget(m_startLaserButton);

    connect(m_emergencyButton, &QPushButton::clicked, this, [this]() {
        // 单击即触发，不做二次确认；先进入界面态，再发出硬件请求
        if (m_controller)
        {
            m_controller->triggerEmergencyStop();
        }
        if (m_emergencyRequest)
        {
            m_emergencyRequest();
        }
        updateUi();
        raise();
    });

    connect(m_confirmResetButton, &QPushButton::clicked, this, [this]() {
        if (m_controller)
        {
            m_controller->confirmReset();
        }
        updateUi();
    });

    connect(m_startLaserButton, &QPushButton::clicked, this, [this]() {
        // 复位确认与开光是两个独立动作，这里只发送开光/开始加工请求
        if (m_startRequest && m_controller &&
            m_controller->state() == EmergencyStopController::State::ResetConfirmed)
        {
            m_startRequest();
        }
    });
}

void EmergencyStopOverlay::updateUi()
{
    if (!m_controller)
    {
        return;
    }

    const EmergencyStopController::State state = m_controller->state();
    const bool emergency = state == EmergencyStopController::State::EmergencyStopped;
    const bool resetConfirmed = state == EmergencyStopController::State::ResetConfirmed;

    m_statusLabel->setVisible(emergency || resetConfirmed);
    m_stepsLabel->setVisible(emergency);
    m_resetStateLabel->setVisible(emergency || resetConfirmed);
    m_confirmResetButton->setVisible(emergency);
    m_startLaserButton->setVisible(emergency || resetConfirmed);

    m_confirmResetButton->setEnabled(m_controller->canConfirmReset());
    m_startLaserButton->setEnabled(resetConfirmed);

    if (emergency)
    {
        m_statusLabel->setText(overlayTr("EMERGENCY STOP ACTIVE"));
        if (m_controller->isMonitoringAvailable())
        {
            m_resetStateLabel->setText(m_controller->isPhysicalResetObserved()
                    ? overlayTr("Physical reset detected. Confirm Reset is available.")
                    : overlayTr("Waiting for physical reset. Confirm Reset stays disabled until the hardware input is released."));
        }
        else
        {
            m_resetStateLabel->setText(
                overlayTr("Hardware monitoring is not active. Confirm Reset only clears the software state."));
        }
    }
    else if (resetConfirmed)
    {
        m_statusLabel->setText(overlayTr("RESET CONFIRMED"));
        m_resetStateLabel->setText(
            overlayTr("Laser output is still off. Start Processing is a separate action and must be clicked explicitly."));
    }

    adjustSize();
    syncVisibility();
}

void EmergencyStopOverlay::syncVisibility()
{
    // 只有急停流程未完成时才出现：正常态完全不存在。
    // 之前这里无条件 show()，浮层从启动起就贴在右上角挡住编辑区。
    const bool active = m_controller &&
        (m_controller->state() == EmergencyStopController::State::EmergencyStopped ||
            m_controller->state() == EmergencyStopController::State::ResetConfirmed);
    if (!active || !m_window || !m_window->isVisible())
    {
        if (isVisible())
        {
            const int state = m_controller ? static_cast<int>(m_controller->state()) : 0;
            APR_DEBUGF("[EmergencyStopOverlay] hidden: state=%d", state);
        }
        hide();
        return;
    }

    if (!isVisible())
    {
        APR_INFOF("[EmergencyStopOverlay] shown: state=%d readOnly=%d",
            static_cast<int>(m_controller->state()),
            m_controller->isReadOnly() ? 1 : 0);
    }
    reposition();
    show();
    raise();
}

void EmergencyStopOverlay::attachToWindow(QWidget* window)
{
    if (!window)
    {
        return;
    }

    if (m_window)
    {
        m_window->removeEventFilter(this);
    }

    m_window = window;
    m_window->installEventFilter(this);
    // 主窗口尚未显示或急停未触发时保持隐藏，可见性统一由 syncVisibility() 裁决
    syncVisibility();
}

bool EmergencyStopOverlay::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_window)
    {
        switch (event->type())
        {
        case QEvent::Move:
        case QEvent::Resize:
            reposition();
            break;
        case QEvent::Show:
        case QEvent::WindowActivate:
            syncVisibility();
            break;
        case QEvent::WindowStateChange:
            // 主窗口最小化时一并隐藏，避免急停入口孤立悬浮在桌面上
            if (m_window->isMinimized())
            {
                hide();
            }
            else
            {
                syncVisibility();
            }
            break;
        case QEvent::Hide:
            hide();
            break;
        case QEvent::Close:
            close();
            break;
        case QEvent::Destroy:
            m_window = nullptr;
            close();
            break;
        default:
            break;
        }
    }

    return QWidget::eventFilter(watched, event);
}

void EmergencyStopOverlay::reposition()
{
    if (!m_window || !m_window->isVisible())
    {
        return;
    }

    const QRect frame = m_window->frameGeometry();
    int x = frame.right() - width() - 16;
    int y = frame.top() + 48;

    if (auto* screen = QGuiApplication::screenAt(QPoint(x, y)))
    {
        const QRect available = screen->availableGeometry();
        x = qBound(available.left(), x, available.right() - width());
        y = qBound(available.top(), y, available.bottom() - height());
    }

    move(x, y);
}
