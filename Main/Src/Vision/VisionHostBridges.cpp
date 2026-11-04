/**
 * @file VisionHostBridges.cpp
 * @brief Vis::HostBridges 的宿主实现。
 *
 * 编译期分两套：ENABLE_HARDWARE 打开时把 DeviceHost 手里的设备指针包成
 * Vision 需要的接口；关闭时全部返回 NotSupported —— 让"没有硬件模块"这件事
 * 在调用点可判定，而不是编不过或假装成功。
 */

// BUILD_VISION=OFF 时该文件整体为空，避免源集合无条件收录本文件却缺少 Vision 头而 C1083。
#if ENABLE_VISION

#include "VisionHostBridges.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <thread>
#include <vector>

#include <QTimer>

#include "../Hardware/DeviceHost.h"

#ifdef ENABLE_HARDWARE
#include "Hardware/Device/HwTypes.h"
#include "Hardware/Device/IGalvoCard.h"
#include "Hardware/Device/IIoModule.h"
#include "Hardware/Device/IMotionCard.h"
#include "Hardware/Device/MotionPlan.h"
#endif

#include "Vision/Service/VisionHostC.h"

#include "Engine2D/Algorithm/EntityTransform.h"
#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/Edit/SceneEditService.h"

namespace
{
    /// 与 Vision 的 nowUs 使用同一个时钟，否则位姿时间戳与帧时间戳不可比。
    int64_t hostNowUs()
    {
        const auto now = std::chrono::system_clock::now().time_since_epoch();
        return std::chrono::duration_cast<std::chrono::microseconds>(now).count();
    }

    constexpr double kDegToRad = 3.14159265358979323846 / 180.0;

    /// 能力缺失（无硬件模块、无对应设备）统一返回 NotSupported，不做静默降级。
    Vis::VisResult hardwareOff(const char* what)
    {
        return Vis::visFail(Vis::VisError::NotSupported, what);
    }

#ifdef ENABLE_HARDWARE
    Vis::VisError toVisError(Hw::HwError error)
    {
        switch (error)
        {
        case Hw::HwError::Ok:
            return Vis::VisError::Ok;
        case Hw::HwError::InvalidArgument:
            return Vis::VisError::InvalidArgument;
        case Hw::HwError::InvalidState:
        case Hw::HwError::NotOpen:
            return Vis::VisError::InvalidState;
        case Hw::HwError::Timeout:
            return Vis::VisError::Timeout;
        case Hw::HwError::Busy:
            return Vis::VisError::Busy;
        case Hw::HwError::NotImplemented:
            return Vis::VisError::NotImplemented;
        case Hw::HwError::SdkNotFound:
            return Vis::VisError::SdkNotFound;
        case Hw::HwError::SdkSymbolMissing:
            return Vis::VisError::SdkSymbolMissing;
        case Hw::HwError::SdkVersionMismatch:
            return Vis::VisError::SdkVersionMismatch;
        default:
            return Vis::VisError::Unknown;
        }
    }

    Vis::VisResult fromHw(Hw::HwError error, const char* what)
    {
        if (error == Hw::HwError::Ok)
        {
            return Vis::visOk();
        }
        return Vis::visFail(toVisError(error), what, static_cast<int32_t>(error));
    }
#endif
}

namespace VisHost
{
    // ==================== PoseHistory ====================

    void PoseHistory::push(int64_t timestampUs, const Vis::Pose2D& pose)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        // 时间戳不单调时丢弃：插值要求 sample 按时间升序，乱序会让 poseAt 取错区间。
        if (m_count > 0)
        {
            const Sample& newest = m_samples[(m_head + kCapacity - 1) % kCapacity];
            if (timestampUs <= newest.timestampUs)
            {
                return;
            }
        }

        Sample& slot = m_samples[m_head];
        slot.timestampUs = timestampUs;
        slot.pose = pose;

        m_head = (m_head + 1) % kCapacity;
        if (m_count < kCapacity)
        {
            ++m_count;
        }
        else
        {
            m_tail = m_head;
        }
    }

    bool PoseHistory::latest(Vis::Pose2D* out) const
    {
        if (!out)
        {
            return false;
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_count == 0)
        {
            return false;
        }
        *out = m_samples[(m_head + kCapacity - 1) % kCapacity].pose;
        return true;
    }

    bool PoseHistory::at(int64_t timestampUs, Vis::Pose2D* out) const
    {
        if (!out)
        {
            return false;
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_count == 0)
        {
            return false;
        }
        if (m_count == 1)
        {
            const Sample& only = m_samples[m_tail];
            if (timestampUs != only.timestampUs)
            {
                return false;
            }
            *out = only.pose;
            return true;
        }

        for (int32_t i = 0; i + 1 < m_count; ++i)
        {
            const int32_t a = (m_tail + i) % kCapacity;
            const int32_t b = (m_tail + i + 1) % kCapacity;
            const Sample& s0 = m_samples[a];
            const Sample& s1 = m_samples[b];
            if (timestampUs < s0.timestampUs || timestampUs > s1.timestampUs)
            {
                continue;
            }
            const int64_t span = s1.timestampUs - s0.timestampUs;
            if (span <= 0)
            {
                *out = s0.pose;
                return true;
            }
            const double t = static_cast<double>(timestampUs - s0.timestampUs) / static_cast<double>(span);
            out->x = s0.pose.x + (s1.pose.x - s0.pose.x) * t;
            out->y = s0.pose.y + (s1.pose.y - s0.pose.y) * t;
            out->thetaDeg = s0.pose.thetaDeg + (s1.pose.thetaDeg - s0.pose.thetaDeg) * t;
            return true;
        }
        return false;
    }

    void PoseHistory::clear()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_count = 0;
        m_head = 0;
        m_tail = 0;
    }

    int32_t PoseHistory::count() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_count;
    }

    // ==================== StagePoseProvider ====================

    StagePoseProvider::StagePoseProvider(DeviceHost* host, QObject* parent)
        : QObject(parent)
        , m_host(host)
    {
        m_timer = new QTimer(this);
        m_timer->setTimerType(Qt::PreciseTimer);
        connect(m_timer, &QTimer::timeout, this, &StagePoseProvider::onSample);
    }

    StagePoseProvider::~StagePoseProvider()
    {
        stop();
    }

    void StagePoseProvider::start(int32_t intervalMs)
    {
        if (m_running)
        {
            return;
        }
        m_intervalMs = intervalMs > 0 ? intervalMs : 5;
        m_history.clear();
        m_timer->start(m_intervalMs);
        m_running = true;
    }

    void StagePoseProvider::stop()
    {
        if (m_timer)
        {
            m_timer->stop();
        }
        m_running = false;
    }

    bool StagePoseProvider::running() const
    {
        return m_running;
    }

    void StagePoseProvider::onSample()
    {
#ifdef ENABLE_HARDWARE
        if (!m_host)
        {
            return;
        }
        Hw::IMotionCard* card = m_host->motionCard();
        if (!card || card->axisCount() < 2)
        {
            return;
        }

        // 轴映射只解析一次：运行中改轴配置属于调机动作，需要重启采样才能反映。
        if (!m_axisMapped)
        {
            m_axisX = 0;
            m_axisY = 1;
            m_axisRotary = -1;
            if (card->axisCount() >= 3)
            {
                const Hw::AxisConfig* third = card->axisConfig(2);
                if (third && third->rotary)
                {
                    m_axisRotary = 2;
                }
            }
            m_axisMapped = true;
        }

        Hw::AxisVector positions;
        if (card->readPositions(positions).error != Hw::HwError::Ok)
        {
            return;
        }
        if (m_axisX >= positions.count || m_axisY >= positions.count)
        {
            return;
        }

        // 未回零时绝对坐标没有机械意义，宁可不给位姿也不给一个会被当成真值的数。
        Hw::AxisStatus status;
        if (card->readAxisStatus(m_axisX, status).error != Hw::HwError::Ok || !status.homed)
        {
            return;
        }
        if (card->readAxisStatus(m_axisY, status).error != Hw::HwError::Ok || !status.homed)
        {
            return;
        }

        Vis::Pose2D pose;
        pose.x = positions.v[m_axisX];
        pose.y = positions.v[m_axisY];
        if (m_axisRotary >= 0 && m_axisRotary < positions.count)
        {
            pose.thetaDeg = positions.v[m_axisRotary];
        }

        m_history.push(hostNowUs(), pose);
#endif
    }

    bool StagePoseProvider::currentPose(Vis::Pose2D* out) const
    {
        return m_history.latest(out);
    }

    bool StagePoseProvider::poseAt(int64_t timestampUs, Vis::Pose2D* out) const
    {
        return m_history.at(timestampUs, out);
    }

    // ==================== StageController ====================

    StageController::StageController(DeviceHost* host)
        : m_host(host)
    {
    }

    Vis::VisResult StageController::moveAbsolute(const Vis::Pose2D& target)
    {
#ifdef ENABLE_HARDWARE
        if (!m_host)
        {
            return hardwareOff("StageController::moveAbsolute: device host is not available");
        }
        Hw::IMotionCard* card = m_host->motionCard();
        if (!card || card->axisCount() < 2)
        {
            return hardwareOff("StageController::moveAbsolute: no motion card with at least two axes");
        }

        Hw::HwError error = card->moveAbsolute(0, target.x, 0.0).error;
        if (error != Hw::HwError::Ok)
        {
            return fromHw(error, "StageController::moveAbsolute: X axis move failed");
        }
        error = card->moveAbsolute(1, target.y, 0.0).error;
        if (error != Hw::HwError::Ok)
        {
            return fromHw(error, "StageController::moveAbsolute: Y axis move failed");
        }

        if (card->axisCount() >= 3)
        {
            const Hw::AxisConfig* third = card->axisConfig(2);
            if (third && third->rotary)
            {
                error = card->moveAbsolute(2, target.thetaDeg, 0.0).error;
                if (error != Hw::HwError::Ok)
                {
                    return fromHw(error, "StageController::moveAbsolute: rotary axis move failed");
                }
            }
        }
        return Vis::visOk();
#else
        (void)target;
        return hardwareOff("StageController::moveAbsolute: hardware support is not compiled in");
#endif
    }

    Vis::VisResult StageController::waitIdle(int32_t timeoutMs)
    {
#ifdef ENABLE_HARDWARE
        if (!m_host)
        {
            return hardwareOff("StageController::waitIdle: device host is not available");
        }
        Hw::IMotionCard* card = m_host->motionCard();
        if (!card)
        {
            return hardwareOff("StageController::waitIdle: no motion card");
        }

        const int32_t limit = timeoutMs > 0 ? timeoutMs : 1000;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(limit);
        const int32_t axisCount = card->axisCount();

        while (std::chrono::steady_clock::now() < deadline)
        {
            bool busy = false;
            for (int32_t axis = 0; axis < axisCount; ++axis)
            {
                Hw::AxisStatus status;
                if (card->readAxisStatus(axis, status).error != Hw::HwError::Ok)
                {
                    return fromHw(Hw::HwError::InvalidState, "StageController::waitIdle: axis status read failed");
                }
                if (status.moving)
                {
                    busy = true;
                    break;
                }
            }
            if (!busy)
            {
                return Vis::visOk();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return Vis::visFail(Vis::VisError::Timeout, "StageController::waitIdle: motion did not settle in time");
#else
        (void)timeoutMs;
        return hardwareOff("StageController::waitIdle: hardware support is not compiled in");
#endif
    }

    // ==================== GalvoController ====================

    GalvoController::GalvoController(DeviceHost* host)
        : m_host(host)
    {
    }

    Vis::VisResult GalvoController::markPoint(double xMm, double yMm)
    {
        Vis::PointF point;
        point.x = xMm;
        point.y = yMm;
        return markPoints(&point, 1);
    }

    Vis::VisResult GalvoController::markPattern(const Vis::PointF* points, int32_t count)
    {
        if (!points || count <= 0)
        {
            return Vis::visFail(Vis::VisError::InvalidArgument,
                                "GalvoController::markPattern: points is null or count is not positive");
        }
        return markPoints(points, count);
    }

    Vis::VisResult GalvoController::markPoints(const Vis::PointF* points, int32_t count)
    {
#ifdef ENABLE_HARDWARE
        if (!m_host)
        {
            return hardwareOff("GalvoController: device host is not available");
        }
        Hw::IGalvoCard* card = m_host->galvoCard();
        if (!card)
        {
            return hardwareOff("GalvoController: no galvo card is attached");
        }

        for (int32_t i = 0; i < count; ++i)
        {
            if (!std::isfinite(points[i].x) || !std::isfinite(points[i].y))
            {
                return Vis::visFail(Vis::VisError::InvalidArgument,
                                    "GalvoController: pattern contains a non-finite coordinate");
            }
        }

        // 每个点一条 RapidTo -> LaserOn -> Dwell -> LaserOff，合成一份计划下发：
        // 逐点 loadPlan/startMark 会让振镜在点与点之间反复起停，标定节拍不可接受。
        constexpr int64_t kMarkDwellUs = 2000;
        std::vector<Hw::MotionCommand> commands;
        commands.reserve(static_cast<size_t>(count) * 4);

        double minX = points[0].x;
        double maxX = points[0].x;
        double minY = points[0].y;
        double maxY = points[0].y;

        for (int32_t i = 0; i < count; ++i)
        {
            Hw::MotionCommand move;
            move.op = Hw::MotionOp::RapidTo;
            move.target.v[0] = points[i].x;
            move.target.v[1] = points[i].y;
            move.target.count = 2;
            commands.push_back(move);

            Hw::MotionCommand on;
            on.op = Hw::MotionOp::LaserOn;
            commands.push_back(on);

            Hw::MotionCommand dwell;
            dwell.op = Hw::MotionOp::Dwell;
            dwell.durationUs = kMarkDwellUs;
            commands.push_back(dwell);

            Hw::MotionCommand off;
            off.op = Hw::MotionOp::LaserOff;
            commands.push_back(off);

            minX = std::min(minX, points[i].x);
            maxX = std::max(maxX, points[i].x);
            minY = std::min(minY, points[i].y);
            maxY = std::max(maxY, points[i].y);
        }

        Hw::MotionPlanHeader header;
        Hw::hwCopyText(header.planId, sizeof(header.planId), "vision.calib.mark");
        Hw::hwCopyText(header.sourceName, sizeof(header.sourceName), "VisionHostBridges");
        header.axisCount = 2;
        header.passCount = 1;
        header.boundsMinX = minX;
        header.boundsMaxX = maxX;
        header.boundsMinY = minY;
        header.boundsMaxY = maxY;
        header.estimatedSeconds = static_cast<double>(count) * 0.01;

        Hw::MotionPlanView view;
        view.header = header;
        view.commands = commands.data();
        view.commandCount = commands.size();

        Hw::HwError error = card->loadPlan(view).error;
        if (error != Hw::HwError::Ok)
        {
            return fromHw(error, "GalvoController: loadPlan rejected the marking pattern");
        }
        error = card->startMark().error;
        if (error != Hw::HwError::Ok)
        {
            return fromHw(error, "GalvoController: startMark failed");
        }

        // 打点是同步语义：标定流程紧接着要拍照测落点，不能留一份还在跑的计划。
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < deadline)
        {
            const Hw::PlanProgress progress = card->markProgress();
            if (progress.state == Hw::PlanState::Completed || progress.state == Hw::PlanState::Idle)
            {
                return Vis::visOk();
            }
            if (progress.state == Hw::PlanState::Aborted)
            {
                return Vis::visFail(Vis::VisError::DeviceFault, "GalvoController: marking was aborted",
                                    static_cast<int32_t>(progress.error));
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        (void)card->abortMark();
        return Vis::visFail(Vis::VisError::Timeout, "GalvoController: marking did not finish in time");
#else
        (void)points;
        (void)count;
        return hardwareOff("GalvoController: hardware support is not compiled in");
#endif
    }

    // ==================== LightController ====================

    LightController::LightController(DeviceHost* host)
        : m_host(host)
    {
    }

    int32_t LightController::channelCount() const
    {
#ifdef ENABLE_HARDWARE
        if (!m_host)
        {
            return 0;
        }
        Hw::IIoModule* io = m_host->ioModule();
        if (!io)
        {
            return 0;
        }
        return io->channelCount(Hw::IoDirection::Output, Hw::IoSignalType::Analog);
#else
        return 0;
#endif
    }

    Vis::VisResult LightController::setBrightness(int32_t channel, double percent)
    {
#ifdef ENABLE_HARDWARE
        if (!m_host)
        {
            return hardwareOff("LightController::setBrightness: device host is not available");
        }
        Hw::IIoModule* io = m_host->ioModule();
        if (!io)
        {
            return hardwareOff("LightController::setBrightness: no IO module is attached");
        }
        if (channel < 0 || channel >= io->channelCount(Hw::IoDirection::Output, Hw::IoSignalType::Analog))
        {
            return Vis::visFail(Vis::VisError::InvalidArgument, "LightController::setBrightness: channel out of range");
        }
        if (!std::isfinite(percent) || percent < 0.0 || percent > 100.0)
        {
            return Vis::visFail(Vis::VisError::InvalidArgument,
                                "LightController::setBrightness: percent must be within 0 and 100");
        }

        const Hw::IoChannelInfo* info = io->channelInfo(Hw::IoDirection::Output, Hw::IoSignalType::Analog, channel);
        if (!info || !(info->analogMax > info->analogMin))
        {
            return Vis::visFail(Vis::VisError::NotSupported,
                                "LightController::setBrightness: channel has no usable analog range");
        }

        const double value = info->analogMin + (info->analogMax - info->analogMin) * (percent / 100.0);
        const Vis::VisResult written = fromHw(io->writeAnalogOutput(channel, value).error,
                                             "LightController::setBrightness: write failed");
        if (written.ok())
        {
            m_percent[static_cast<size_t>(channel)] = percent;
        }
        return written;
#else
        (void)channel;
        (void)percent;
        return hardwareOff("LightController::setBrightness: hardware support is not compiled in");
#endif
    }

    Vis::VisResult LightController::setEnabled(int32_t channel, bool enabled)
    {
#ifdef ENABLE_HARDWARE
        if (!m_host)
        {
            return hardwareOff("LightController::setEnabled: device host is not available");
        }
        Hw::IIoModule* io = m_host->ioModule();
        if (!io)
        {
            return hardwareOff("LightController::setEnabled: no IO module is attached");
        }
        if (channel < 0 || channel >= io->channelCount(Hw::IoDirection::Output, Hw::IoSignalType::Analog))
        {
            return Vis::visFail(Vis::VisError::InvalidArgument, "LightController::setEnabled: channel out of range");
        }

        // 开关与亮度共用一路模拟量：关灯写量程下限，开灯恢复上一次亮度。
        // 用数字输出去开关会让"调亮度"和"开关"落在两个不同的通道域，
        // 调用方给同一个 channel 时行为无法解释。
        const Hw::IoChannelInfo* info = io->channelInfo(Hw::IoDirection::Output, Hw::IoSignalType::Analog, channel);
        if (!info || !(info->analogMax > info->analogMin))
        {
            return Vis::visFail(Vis::VisError::NotSupported,
                                "LightController::setEnabled: channel has no usable analog range");
        }

        if (!enabled)
        {
            return fromHw(io->writeAnalogOutput(channel, info->analogMin).error,
                          "LightController::setEnabled: switch off failed");
        }

        const size_t index = static_cast<size_t>(channel);
        if (index >= m_percent.size())
        {
            return Vis::visFail(Vis::VisError::InvalidArgument, "LightController::setEnabled: channel out of range");
        }
        const double percent = m_percent[index];
        const double value = info->analogMin + (info->analogMax - info->analogMin) * (percent / 100.0);
        return fromHw(io->writeAnalogOutput(channel, value).error, "LightController::setEnabled: restore failed");
#else
        (void)channel;
        (void)enabled;
        return hardwareOff("LightController::setEnabled: hardware support is not compiled in");
#endif
    }

    // ==================== WorkpieceTransformSink ====================

    WorkpieceTransformSink::WorkpieceTransformSink(Eg::SceneManager* sceneManager, SceneEditService* editService)
        : m_sceneManager(sceneManager)
        , m_editService(editService)
    {
    }

    Vis::VisResult WorkpieceTransformSink::apply(const Vis::Sim2D& transform, Vis::Frame from, Vis::Frame to)
    {
        if (!m_sceneManager || !m_editService)
        {
            return Vis::visFail(Vis::VisError::NotSupported,
                                "WorkpieceTransformSink::apply: scene manager or edit service is not available");
        }
        if (!std::isfinite(transform.scale) || !std::isfinite(transform.thetaDeg)
            || !std::isfinite(transform.tx) || !std::isfinite(transform.ty) || transform.scale <= 0.0)
        {
            return Vis::visFail(Vis::VisError::InvalidArgument,
                                "WorkpieceTransformSink::apply: transform has a non-finite component or non-positive scale");
        }

        // 回写只支持"落到图纸坐标系"这一件事；其它目标说明调用方用错了回写端。
        if (to != Vis::Frame::Document)
        {
            return Vis::visFail(Vis::VisError::NotSupported,
                                "WorkpieceTransformSink::apply: only Frame::Document is writable by this host");
        }
        if (from == to)
        {
            return Vis::visFail(Vis::VisError::InvalidArgument,
                                "WorkpieceTransformSink::apply: source frame equals target frame");
        }

        std::vector<Eg::EntityId> ids;
        const Eg::VecSyEntityPtr entities = m_sceneManager->getAllEntities();
        ids.reserve(entities.size());
        for (Eg::SyEntity* entity : entities)
        {
            if (entity)
            {
                ids.push_back(entity->getId());
            }
        }
        if (ids.empty())
        {
            return Vis::visFail(Vis::VisError::InvalidState,
                                "WorkpieceTransformSink::apply: the document has no entities to transform");
        }

        Eg::SceneManager* scene = m_sceneManager;
        const double thetaRad = transform.thetaDeg * kDegToRad;
        const double tx = transform.tx;
        const double ty = transform.ty;
        const double scale = transform.scale;

        // 通过编辑服务提交，与手工变换共用同一条撤销栈。
        m_editService->transformEntities(
            ids, [scene, ids, tx, ty, thetaRad, scale]() {
                EntityTransform transformOp(scene);
                transformOp.transformMultiple(ids, tx, ty, thetaRad, scale, scale, 0.0, 0.0);
            },
            "Vision workpiece alignment");

        m_lastApplied = transform;
        m_applied = true;
        return Vis::visOk();
    }

    Vis::VisResult WorkpieceTransformSink::revert()
    {
        if (!m_applied)
        {
            return Vis::visFail(Vis::VisError::InvalidState,
                                "WorkpieceTransformSink::revert: no applied transform to revert");
        }
        if (!m_sceneManager || !m_editService)
        {
            return Vis::visFail(Vis::VisError::NotSupported,
                                "WorkpieceTransformSink::revert: scene manager or edit service is not available");
        }

        // 逆变换：p' = s * R(theta) * p + t  =>  p = R(-theta) * (p' - t) / s
        const double thetaRad = -m_lastApplied.thetaDeg * kDegToRad;
        const double inverseScale = 1.0 / m_lastApplied.scale;
        const double c = std::cos(thetaRad);
        const double s = std::sin(thetaRad);
        const double tx = -(c * m_lastApplied.tx - s * m_lastApplied.ty) * inverseScale;
        const double ty = -(s * m_lastApplied.tx + c * m_lastApplied.ty) * inverseScale;

        std::vector<Eg::EntityId> ids;
        const Eg::VecSyEntityPtr entities = m_sceneManager->getAllEntities();
        ids.reserve(entities.size());
        for (Eg::SyEntity* entity : entities)
        {
            if (entity)
            {
                ids.push_back(entity->getId());
            }
        }
        if (ids.empty())
        {
            m_applied = false;
            return Vis::visOk();
        }

        Eg::SceneManager* scene = m_sceneManager;
        m_editService->transformEntities(
            ids, [scene, ids, tx, ty, thetaRad, inverseScale]() {
                EntityTransform transformOp(scene);
                transformOp.transformMultiple(ids, tx, ty, thetaRad, inverseScale, inverseScale, 0.0, 0.0);
            },
            "Vision workpiece alignment revert");

        m_applied = false;
        return Vis::visOk();
    }

    // ---- C ABI 注册表：把桥对象接到 Vision 的函数指针表上 ----

    namespace
    {
        /// 一次装配期只存在一个组合根，桥对象指针集中放在静态上下文里，
        /// Vision 只持有 user 指针，不参与这些对象的生命周期。
        struct BridgeContext
        {
            StagePoseProvider* pose = nullptr;
            StageController* stage = nullptr;
            GalvoController* galvo = nullptr;
            LightController* light = nullptr;
            WorkpieceTransformSink* sink = nullptr;
        };

        BridgeContext& context()
        {
            static BridgeContext instance;
            return instance;
        }

        int32_t codeOf(const Vis::VisResult& result)
        {
            return static_cast<int32_t>(result.error);
        }

        int32_t unsupportedCode()
        {
            return static_cast<int32_t>(Vis::VisError::NotSupported);
        }

        int32_t unavailableCode()
        {
            return static_cast<int32_t>(Vis::VisError::InvalidState);
        }

        int32_t onCurrentPose(void* user, VisionHostPose* out)
        {
            const BridgeContext& ctx = *static_cast<BridgeContext*>(user);
            if (!ctx.pose)
            {
                return unsupportedCode();
            }
            if (!out)
            {
                return static_cast<int32_t>(Vis::VisError::InvalidArgument);
            }
            Vis::Pose2D pose;
            if (!ctx.pose->currentPose(&pose))
            {
                return unavailableCode();
            }
            out->x = pose.x;
            out->y = pose.y;
            out->thetaDeg = pose.thetaDeg;
            return 0;
        }

        int32_t onPoseAt(void* user, int64_t timestampUs, VisionHostPose* out)
        {
            const BridgeContext& ctx = *static_cast<BridgeContext*>(user);
            if (!ctx.pose)
            {
                return unsupportedCode();
            }
            if (!out)
            {
                return static_cast<int32_t>(Vis::VisError::InvalidArgument);
            }
            Vis::Pose2D pose;
            if (!ctx.pose->poseAt(timestampUs, &pose))
            {
                return unavailableCode();
            }
            out->x = pose.x;
            out->y = pose.y;
            out->thetaDeg = pose.thetaDeg;
            return 0;
        }

        int32_t onMoveAbsolute(void* user, const VisionHostPose* target)
        {
            const BridgeContext& ctx = *static_cast<BridgeContext*>(user);
            if (!ctx.stage)
            {
                return unsupportedCode();
            }
            if (!target)
            {
                return static_cast<int32_t>(Vis::VisError::InvalidArgument);
            }
            Vis::Pose2D pose;
            pose.x = target->x;
            pose.y = target->y;
            pose.thetaDeg = target->thetaDeg;
            return codeOf(ctx.stage->moveAbsolute(pose));
        }

        int32_t onWaitIdle(void* user, int32_t timeoutMs)
        {
            const BridgeContext& ctx = *static_cast<BridgeContext*>(user);
            if (!ctx.stage)
            {
                return unsupportedCode();
            }
            return codeOf(ctx.stage->waitIdle(timeoutMs));
        }

        int32_t onMarkPoint(void* user, double xMm, double yMm)
        {
            const BridgeContext& ctx = *static_cast<BridgeContext*>(user);
            if (!ctx.galvo)
            {
                return unsupportedCode();
            }
            return codeOf(ctx.galvo->markPoint(xMm, yMm));
        }

        int32_t onMarkPattern(void* user, const VisionHostPoint* points, int32_t count)
        {
            const BridgeContext& ctx = *static_cast<BridgeContext*>(user);
            if (!ctx.galvo)
            {
                return unsupportedCode();
            }
            if (!points || count <= 0)
            {
                return static_cast<int32_t>(Vis::VisError::InvalidArgument);
            }
            std::vector<Vis::PointF> converted;
            converted.reserve(static_cast<size_t>(count));
            for (int32_t i = 0; i < count; ++i)
            {
                Vis::PointF item;
                item.x = points[i].x;
                item.y = points[i].y;
                converted.push_back(item);
            }
            return codeOf(ctx.galvo->markPattern(converted.data(), count));
        }

        int32_t onLightChannelCount(void* user)
        {
            const BridgeContext& ctx = *static_cast<BridgeContext*>(user);
            return ctx.light ? ctx.light->channelCount() : 0;
        }

        int32_t onSetBrightness(void* user, int32_t channel, double percent)
        {
            const BridgeContext& ctx = *static_cast<BridgeContext*>(user);
            if (!ctx.light)
            {
                return unsupportedCode();
            }
            return codeOf(ctx.light->setBrightness(channel, percent));
        }

        int32_t onSetEnabled(void* user, int32_t channel, int32_t enabled)
        {
            const BridgeContext& ctx = *static_cast<BridgeContext*>(user);
            if (!ctx.light)
            {
                return unsupportedCode();
            }
            return codeOf(ctx.light->setEnabled(channel, enabled != 0));
        }

        int32_t onApplyTransform(void* user, const VisionHostSim2D* transform, int32_t fromFrame,
                                 int32_t toFrame)
        {
            const BridgeContext& ctx = *static_cast<BridgeContext*>(user);
            if (!ctx.sink)
            {
                return unsupportedCode();
            }
            if (!transform)
            {
                return static_cast<int32_t>(Vis::VisError::InvalidArgument);
            }
            Vis::Sim2D sim;
            sim.scale = transform->scale;
            sim.thetaDeg = transform->thetaDeg;
            sim.tx = transform->tx;
            sim.ty = transform->ty;
            return codeOf(ctx.sink->apply(sim, static_cast<Vis::Frame>(fromFrame),
                                          static_cast<Vis::Frame>(toFrame)));
        }

        int32_t onRevertTransform(void* user)
        {
            const BridgeContext& ctx = *static_cast<BridgeContext*>(user);
            if (!ctx.sink)
            {
                return unsupportedCode();
            }
            return codeOf(ctx.sink->revert());
        }
    }

    bool registerBridges(StagePoseProvider* poseProvider,
                         StageController* stageController,
                         GalvoController* galvoController,
                         LightController* lightController,
                         WorkpieceTransformSink* transformSink)
    {
        BridgeContext& ctx = context();
        ctx.pose = poseProvider;
        ctx.stage = stageController;
        ctx.galvo = galvoController;
        ctx.light = lightController;
        ctx.sink = transformSink;

        VisionHostBridges table = {};
        table.structSize = static_cast<uint32_t>(sizeof(VisionHostBridges));
        table.user = &ctx;
        table.currentPose = onCurrentPose;
        table.poseAt = onPoseAt;
        table.moveAbsolute = onMoveAbsolute;
        table.waitIdle = onWaitIdle;
        table.markPoint = onMarkPoint;
        table.markPattern = onMarkPattern;
        table.lightChannelCount = onLightChannelCount;
        table.setBrightness = onSetBrightness;
        table.setEnabled = onSetEnabled;
        table.applyTransform = onApplyTransform;
        table.revertTransform = onRevertTransform;

        return Vision_SetHostBridges(&table) == 0;
    }

    void unregisterBridges()
    {
        // 全空表仍然带合法 structSize：Vision 侧会解绑全部注入槽而不注册新对象。
        VisionHostBridges table = {};
        table.structSize = static_cast<uint32_t>(sizeof(VisionHostBridges));
        (void)Vision_SetHostBridges(&table);
    }
}

#endif  // ENABLE_VISION
