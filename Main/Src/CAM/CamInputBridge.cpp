/**
 * @file CamInputBridge.cpp
 * @brief Engine2D 类型 → CAM ToolpathJobInput 的唯一桥接点。
 *
 * 条件编译：仅在 BUILD_CAM=ON 时编进 Main。
 * 这里可以安全 include Engine2D/Cam/GeometryExtractor.h ——
 * CAM/CAM/ 自身不得 include 这个头。
 */

#include "CamInputBridge.h"

#include "CAM/ToolpathJobInput.h"

#include "Engine/Process/ProcessParams.h"
#include "Engine/SyEntity/SyEntity.h"
#include "Engine2D/Cam/ContourContainment.h"
#include "Engine2D/Cam/GeometryExtractor.h"
#include "Engine2D/Cam/KerfCompensator.h"
#include "Engine2D/Cam/ToolpathTypes.h"
#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/Edit/LayerSnapshot.h"
#include "Engine2D/Interaction/LayerManager.h"
#include "Engine2D/SyEntity/SyImage.h"

#include "Hardware/ToolpathJobSpec.h"

#ifdef BUILD_RASTER
#include "Raster/RasterApi.h"
#include "Raster/RasterTypes.h"
#endif

#include <QDebug>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace CamBridge
{
    // ============================================================
    //  ABI 自检：首次调用时打印 CAM 的版本号与关键结构 sizeof
    //  C++ 内部 ABI 要求同工具链构建，这条日志是唯一的运行期对照点
    // ============================================================

    void logAbiSelfCheck()
    {
        static bool logged = false;
        if (logged)
        {
            return;
        }
        logged = true;

        const Cam::CamAbiInfo info = Cam::camAbiInfo();
        qInfo("[CAM] ABI self-check: version=%d sizeof(PathSegment)=%zu "
              "sizeof(Contour)=%zu sizeof(EntitySnapshot)=%zu "
              "sizeof(ToolpathJobInput)=%zu sizeof(CamPlan)=%zu sizeof(CamResult)=%zu",
              info.version,
              info.sizeofPathSegment,
              info.sizeofContour,
              info.sizeofEntitySnapshot,
              info.sizeofToolpathJobInput,
              info.sizeofCamPlan,
              info.sizeofCamResult);
    }

    // ============================================================
    //  内部：把 Eg::Cam::Contour 转成 Cam::Contour
    //  ============================================================

    /// 角度单位换算：Engine2D 几何层用弧度，CAM 契约用度
    static double radiansToDegrees(double radians)
    {
        return radians * (180.0 / 3.14159265358979323846);
    }

    static Cam::Contour convertContour(
        const Eg::Cam::Contour& src)
    {
        Cam::Contour out;
        out.closed = src.closed;
        out.hole = src.hole;
        out.sourceEntityId = src.sourceEntityId;
        out.sourceLayerId = src.sourceLayerId;
        out.length = src.length;
        out.segments.reserve(src.segments.size());
        for (const auto& seg : src.segments)
        {
            Cam::PathSegment pseg;
            pseg.type = (seg.type == Eg::Cam::PathSegment::Type::Arc)
                ? Cam::PathSegment::Type::Arc
                : Cam::PathSegment::Type::Line;
            pseg.ptStart = seg.ptStart;
            pseg.ptEnd = seg.ptEnd;
            pseg.ptCenter = seg.ptCenter;
            pseg.dRadius = seg.dRadius;
            pseg.dStartAngle = radiansToDegrees(seg.dStartAngle);
            pseg.dEndAngle = radiansToDegrees(seg.dEndAngle);
            pseg.counterClockwise = seg.counterClockwise;
            pseg.closed = seg.closed;
            out.segments.push_back(pseg);
        }
        return out;
    }

    static Cam::ProcessParams convertProcessParams(
        const Eg::ProcessParams& src,
        double fallbackRapidSpeedMmPerSec,
        double fallbackChordToleranceMm)
    {
        Cam::ProcessParams out;
        out.powerPercent = src.powerPercent;
        out.frequencyHz = src.frequencyHz;
        out.pulseWidthUs = src.pulseWidthUs;
        out.waveformIndex = src.waveformIndex;
        out.speedMmPerSec = src.speedMmPerSec;
        // Engine 约定 rapidSpeed=0 表示使用工程默认值；CAM 只接受具体数值，
        // 因此在这里归一化，避免默认配置被 CAM 的 valid() 判为非法
        out.rapidSpeedMmPerSec = src.rapidSpeedMmPerSec > 0.0
            ? src.rapidSpeedMmPerSec
            : (fallbackRapidSpeedMmPerSec > 0.0 ? fallbackRapidSpeedMmPerSec : 300.0);
        out.passes = src.passes;
        out.enabled = src.enabled;
        // Eg::ProcessParams 没有弦高字段，使用作业级 ToolpathJobSpec 的容差
        out.chordToleranceMm = fallbackChordToleranceMm > 0.0
            ? fallbackChordToleranceMm : 0.01;
        return out;
    }

#ifdef BUILD_RASTER
    // ============================================================
    //  位图装配：取图 → 配方 → 单位换算 → Raster 出扫描线段
    //  CAM 只消费换算后的毫米坐标，不重新读取位图
    // ============================================================

    /// 像素坐标 → 用户坐标（双线性四点映射，兼容旋转/斜切）
    static Ut::Vec2d mapImagePixel(const Eg::SyImage& image, double u, double v)
    {
        const double fu = image.nWidth > 0 ? u / static_cast<double>(image.nWidth) : 0.0;
        const double fv = image.nHeight > 0 ? v / static_cast<double>(image.nHeight) : 0.0;
        const Ut::Vec2d axisU = image.topRight - image.topLeft;
        const Ut::Vec2d axisV = image.bottomLeft - image.topLeft;
        return Ut::Vec2d(image.topLeft.x() + axisU.x() * fu + axisV.x() * fv,
                         image.topLeft.y() + axisU.y() * fu + axisV.y() * fv);
    }

    /// Eg::ProcessParams → RasterScanlineParams；线距按像素当量换算成像素
    static RasterScanlineParams makeScanlineParams(const Eg::SyImage& image,
                                                   const Eg::ProcessParams& process)
    {
        const double pitchX = image.pixelPitchMmX();
        const double pitchY = image.pixelPitchMmY();
        const double pitch = pitchX > 0.0 ? pitchX : pitchY;

        RasterScanlineParams params{};
        params.structSize = sizeof(RasterScanlineParams);
        const double spacingMm = process.fillIntervalMm > 0.0 ? process.fillIntervalMm : 0.1;
        params.lineSpacing = pitch > 0.0 ? spacingMm / pitch : spacingMm;
        params.angleDeg = process.fillAngleDeg;
        params.passes = process.passes > 0 ? process.passes : 1;
        params.speed = process.speedMmPerSec;
        params.powerMin = 0.0;
        params.powerMax = process.powerPercent / 100.0;
        params.bidirectional = process.fillMode == Eg::FillMode::Zigzag ? 1 : 0;
        params.overscan = 0.0;
        params.pixelPitchMm = pitch;
        return params;
    }

    /// SyImage 像素 → 8 位灰度：RGB24 按 R,G,B 解释，RGBA32 先按白底合成 alpha
    static bool imageToGray8(const Eg::SyImage& image, std::vector<uint8_t>& gray)
    {
        gray.clear();
        const size_t pixelCount =
            static_cast<size_t>(image.nWidth) * static_cast<size_t>(image.nHeight);
        if (pixelCount == 0 || image.pixelDataSize() < pixelCount)
        {
            return false;
        }
        const size_t bytesPerPixel = image.pixelDataSize() / pixelCount;
        const uint8_t* src = image.pixelData();
        if (src == nullptr)
        {
            return false;
        }

        // 与 OpenCV RGB2GRAY 同权重，保证同一张图在这里与 Raster 内部得到同一灰度
        const double weightR = 0.299;
        const double weightG = 0.587;
        const double weightB = 0.114;
        auto clampByte = [](double value)
        {
            if (value <= 0.0) return static_cast<uint8_t>(0);
            if (value >= 255.0) return static_cast<uint8_t>(255);
            return static_cast<uint8_t>(value + 0.5);
        };

        gray.resize(pixelCount);
        for (size_t i = 0; i < pixelCount; ++i)
        {
            const uint8_t* p = src + i * bytesPerPixel;
            switch (image.ePixelFormat)
            {
            case Eg::SyPixelFormat::Gray8:
                gray[i] = p[0];
                break;
            case Eg::SyPixelFormat::RGB24:
                gray[i] = clampByte(weightR * p[0] + weightG * p[1] + weightB * p[2]);
                break;
            case Eg::SyPixelFormat::RGBA32:
            {
                // 透明区域按白纸处理，避免全透明像素被当成纯黑而出光
                const double alpha = static_cast<double>(p[3]) / 255.0;
                const double r = p[0] * alpha + 255.0 * (1.0 - alpha);
                const double g = p[1] * alpha + 255.0 * (1.0 - alpha);
                const double b = p[2] * alpha + 255.0 * (1.0 - alpha);
                gray[i] = clampByte(weightR * r + weightG * g + weightB * b);
                break;
            }
            default:
                gray.clear();
                return false;
            }
        }
        return true;
    }

    /// 把位图实体装配为 CAM 扫描线段（像素坐标在冲压前换算成 mm）
    static void fillRasterScanlines(const Eg::SyImage& image,
                                    const Eg::ProcessParams& process,
                                    Cam::EntitySnapshot& snapshot)
    {
        std::vector<uint8_t> gray;
        if (!imageToGray8(image, gray))
        {
            snapshot.unmachinableReason = "unsupported raster pixel format";
            return;
        }

        RasterImageView view{};
        view.structSize = sizeof(RasterImageView);
        view.width = image.nWidth;
        view.height = image.nHeight;
        view.channels = 1;
        view.format = RASTER_FORMAT_GRAY8;
        view.data = gray.data();
        view.stride = 0;

        RasterImage rasterImage = nullptr;
        const RasterStatus createStatus = Raster_CreateImageFromView(&view, &rasterImage);
        if (createStatus != RASTER_OK || rasterImage == nullptr)
        {
            snapshot.unmachinableReason = Raster_StatusText(createStatus);
            return;
        }

        const RasterScanlineParams params = makeScanlineParams(image, process);
        RasterJobList jobs = nullptr;
        const RasterStatus status = Raster_ScanlineJobs(rasterImage, &params, &jobs);
        Raster_DestroyImage(rasterImage);
        if (status != RASTER_OK || jobs == nullptr)
        {
            snapshot.unmachinableReason = Raster_StatusText(status);
            return;
        }

        int32_t count = 0;
        Raster_JobListCount(jobs, &count);
        snapshot.scanlines.reserve(static_cast<size_t>(count));
        for (int32_t i = 0; i < count; ++i)
        {
            RasterScanlineSegment seg{};
            if (Raster_JobListGet(jobs, i, &seg) != RASTER_OK)
            {
                break;
            }
            Cam::ScanlineSegment line;
            line.ptStart = mapImagePixel(image, seg.x0, seg.y);
            line.ptEnd = mapImagePixel(image, seg.x1, seg.y1);
            line.powerPercent = seg.laserOn != 0 ? seg.power * 100.0 : 0.0;
            line.speedMmPerSec = seg.speed;
            line.passIndex = seg.pass;
            line.dir = seg.dir;
            line.laserOn = seg.laserOn != 0;
            snapshot.scanlines.push_back(line);
        }
        Raster_DestroyJobList(jobs);
        snapshot.unmachinableReason.clear();
    }
#endif

    // ============================================================
    //  buildJobInput 主实现
    // ============================================================

    Cam::ToolpathJobInput buildJobInput(
        const Eg::SceneManager& scene,
        const LayerManager* layers,
        const ToolpathJobSpec& spec)
    {
        Cam::ToolpathJobInput input;

        // 首次构造输入时做一次 ABI 自检（后续调用直接返回）
        logAbiSelfCheck();

        // ---- 作业标识 ----
        std::strncpy(input.jobId, spec.jobId.toUtf8().constData(), sizeof(input.jobId) - 1);
        input.jobId[sizeof(input.jobId) - 1] = '\0';

        // ---- 全局设置 ----
        input.selectionOnly = spec.selectionOnly;
        input.selectionCount = (int32_t)spec.selectedEntityIds.size();
        input.emitArcs = spec.emitArcs;
        input.skipHiddenLayers = spec.skipHiddenLayers;
        input.skipLockedLayers = spec.skipLockedLayers;
        input.emitTrailingLaserOff = spec.emitTrailingLaserOff;
        input.failOnUnmachinable = spec.failOnUnmachinableEntity;

        // 坐标系：输入全程用户坐标，桥接层不做任何机器坐标变换；
        // "用户坐标 → 机器坐标"由 Main 侧发射器在 M2 一步完成

        // ---- 选集：selectionOnly=true 时构造 set ----
        std::unordered_set<int64_t> selectedIds;
        if (input.selectionOnly)
        {
            for (int64_t id : spec.selectedEntityIds)
            {
                selectedIds.insert(id);
            }
        }

        // ---- 图层顺序（来自 LayerManager 快照，不来自 SceneManager 图元自然顺序）----
        std::vector<int> layerOrder;
        if (layers)
        {
            const LayerDocumentSnapshot snapshot = layers->captureDocument();
            layerOrder = snapshot.layerOrder;
        }

        // ---- 先为每个图层创建空的 LayerBucket ----
        auto layerBucketFor = [&](int layerId) -> Cam::ToolpathJobInput::LayerBucket& {
            for (auto& lb : input.layers)
            {
                if (lb.layerId == layerId) return lb;
            }
            Cam::ToolpathJobInput::LayerBucket lb;
            lb.layerId = layerId;
            if (layers)
            {
                lb.visible = layers->isLayerVisible(layerId);
                lb.locked = layers->isLayerLocked(layerId);
                lb.outputEnabled = layers->isLayerOutput(layerId);
            }
            // 工艺参数：显式覆盖优先，否则默认
            auto it = spec.layerParams.constFind(layerId);
            lb.params = convertProcessParams(
                (it != spec.layerParams.constEnd())
                    ? static_cast<const Eg::ProcessParams&>(it.value())
                    : static_cast<const Eg::ProcessParams&>(spec.defaultParams),
                spec.rapidSpeedMmPerSec,
                spec.chordToleranceMm);
            input.layers.push_back(std::move(lb));
            return input.layers.back();
        };

        // ---- 遍历所有图元，分桶 ----
        // 轮廓先以 Engine2D 类型暂存：内外环归属与割缝补偿都需要跨图元统一计算，
        // 因此不能在单图元转换时立刻定型
        std::unordered_map<uint64_t, std::vector<Eg::Cam::Contour>> egContoursByEntity;
        auto allEntities = scene.getAllEntities();
        for (const auto* entity : allEntities)
        {
            if (!entity || !entity->isValid()) continue;

            // 选集过滤（桥接层就过滤掉，CAM 内部看不到这些）
            if (input.selectionOnly && selectedIds.find(entity->id) == selectedIds.end())
            {
                continue;
            }

            const int layerId = layers ? layers->getEntityLayer(entity) : 0;
            auto& bucket = layerBucketFor(layerId);

            Cam::EntitySnapshot snapshot;
            snapshot.entityId = entity->id;
            snapshot.layerId = layerId;
            // 图元级隐藏/锁定必须透传，CAM 内按 skipHidden/skipLocked 过滤
            snapshot.visible = entity->visible();
            snapshot.locked = entity->locked();

            // 几何提取：加工链路的唯一入口
            Eg::Cam::ExtractOptions extractOpts;
            extractOpts.geometry.dDiscretize = spec.chordToleranceMm;
            const Eg::Cam::ExtractResult extracted = Eg::Cam::GeometryExtractor::extract(entity, extractOpts);

            // 提取器给出的不可加工原因必须透传，避免 CAM 只能看到「少了一块」却说不清为什么
            if (extracted.reason != nullptr)
            {
                snapshot.unmachinableReason = extracted.reason;
            }

            switch (extracted.kind)
            {
            case Eg::Cam::GeometryKind::Contour:
            {
                snapshot.kind = Cam::EntitySnapshot::GeometryKind::Contour;
                egContoursByEntity[entity->id] = extracted.contours;
                break;
            }
            case Eg::Cam::GeometryKind::Point:
            {
                snapshot.kind = Cam::EntitySnapshot::GeometryKind::Point;
                snapshot.point = extracted.point;
                break;
            }
            case Eg::Cam::GeometryKind::Fillable:
            {
                snapshot.kind = Cam::EntitySnapshot::GeometryKind::Fillable;
                // 边界环也塞进去，CAM M0 会跳过，但结构上完整
                for (const auto& contour : extracted.contours)
                {
                    snapshot.fillRegion.boundaries.push_back(convertContour(contour));
                }
                break;
            }
            case Eg::Cam::GeometryKind::Raster:
            {
                snapshot.kind = Cam::EntitySnapshot::GeometryKind::Raster;
#ifdef BUILD_RASTER
                // 位图实体：主程序负责取图/配方/换算，Raster 出段，CAM 只编排运动
                if (entity != nullptr && entity->eType == Eg::EType::IMAGE)
                {
                    const Eg::SyImage* image = static_cast<const Eg::SyImage*>(entity);
                    auto it = spec.layerParams.constFind(layerId);
                    const Eg::ProcessParams& process =
                        (it != spec.layerParams.constEnd())
                            ? static_cast<const Eg::ProcessParams&>(it.value())
                            : static_cast<const Eg::ProcessParams&>(spec.defaultParams);
                    fillRasterScanlines(*image, process, snapshot);
                }
                else
                {
                    snapshot.unmachinableReason = "raster entity is not SyImage";
                }
#else
                snapshot.unmachinableReason = "raster scanline assembly requires BUILD_RASTER";
#endif
                break;
            }
            case Eg::Cam::GeometryKind::Container:
                // 容器本身没有几何，跳过
                continue;
            case Eg::Cam::GeometryKind::Unsupported:
            default:
                // 提取器明确判定不可加工：保持 Unsupported，由 CAM 计入清单并报错，
                // 不得降级成 Contour 静默加工（与 v1 MotionPlanCompiler 语义一致）
                snapshot.kind = Cam::EntitySnapshot::GeometryKind::Unsupported;
                break;
            }

            bucket.entities.push_back(std::move(snapshot));
        }

        // ---- 内外环归属与割缝补偿：按图层跨图元统一处理 ----
        for (auto& bucket : input.layers)
        {
            // 1) 真正计算 hole：区域内所有闭合轮廓一起做嵌套解析
            std::vector<Eg::Cam::Contour> flatContours;
            std::vector<Eg::Cam::Contour*> contourSlots;
            for (auto& snapshot : bucket.entities)
            {
                if (snapshot.kind != Cam::EntitySnapshot::GeometryKind::Contour)
                {
                    continue;
                }
                auto it = egContoursByEntity.find(snapshot.entityId);
                if (it == egContoursByEntity.end())
                {
                    continue;
                }
                for (auto& contour : it->second)
                {
                    contourSlots.push_back(&contour);
                    flatContours.push_back(contour);
                }
            }
            if (!flatContours.empty())
            {
                Eg::Cam::ContourContainment::fillHoleFlags(flatContours);
                for (std::size_t i = 0; i < flatContours.size(); ++i)
                {
                    *contourSlots[i] = flatContours[i];
                }
            }

            // 2) 割缝补偿：kerfMm 是单边补偿值（不是缝宽），正负映射到下刀内外侧；
            //    KerfOptions.kerfWidth 取两倍只是本层内部换算，用于等距偏置
            const Eg::ProcessParams& egParams = [&]() -> const Eg::ProcessParams& {
                auto it = spec.layerParams.constFind(bucket.layerId);
                return (it != spec.layerParams.constEnd())
                    ? static_cast<const Eg::ProcessParams&>(it.value())
                    : static_cast<const Eg::ProcessParams&>(spec.defaultParams);
            }();
            const double kerfHalf = std::fabs(egParams.kerfMm);
            if (kerfHalf > 0.0)
            {
                Eg::Cam::KerfOptions kerfOptions;
                kerfOptions.kerfWidth = kerfHalf * 2.0;
                kerfOptions.side = egParams.kerfMm >= 0.0
                    ? Eg::Cam::KerfSide::Outside
                    : Eg::Cam::KerfSide::Inside;
                // 偏置只在一处发生：轮廓已在下面就地补偿完成，
                // CAM 侧不得再按 KerfParams 二次偏置，否则会叠加成两倍补偿
                bucket.kerf.enabled = false;

                int dropped = 0;
                int split = 0;
                for (auto& snapshot : bucket.entities)
                {
                    if (snapshot.kind != Cam::EntitySnapshot::GeometryKind::Contour)
                    {
                        continue;
                    }
                    auto it = egContoursByEntity.find(snapshot.entityId);
                    if (it == egContoursByEntity.end())
                    {
                        continue;
                    }
                    const Eg::Cam::KerfResult kerfResult =
                        Eg::Cam::KerfCompensator::apply(it->second, kerfOptions);
                    it->second = kerfResult.contours;
                    dropped += kerfResult.droppedLoopCount;
                    split += kerfResult.splitLoopCount;
                    if (kerfResult.status != 0)
                    {
                        qWarning("[CAM] kerf compensation failed: layer=%d entity=%llu status=%d",
                            bucket.layerId,
                            static_cast<unsigned long long>(snapshot.entityId),
                            kerfResult.status);
                    }
                }
                qInfo("[CAM] kerf compensation: layer=%d width=%.3fmm side=%d dropped=%d split=%d",
                    bucket.layerId, kerfOptions.kerfWidth,
                    static_cast<int>(kerfOptions.side), dropped, split);
            }

            // 3) 定型为 CAM 轮廓
            for (auto& snapshot : bucket.entities)
            {
                if (snapshot.kind != Cam::EntitySnapshot::GeometryKind::Contour)
                {
                    continue;
                }
                auto it = egContoursByEntity.find(snapshot.entityId);
                snapshot.contours.clear();
                if (it == egContoursByEntity.end())
                {
                    continue;
                }
                for (const auto& contour : it->second)
                {
                    snapshot.contours.push_back(convertContour(contour));
                }
            }
        }

        // ---- 按图层顺序重排 LayerBucket ----
        // 以 LayerDocumentSnapshot.layerOrder 为准（文档绘制顺序），
        // 不在 layerOrder 里的图层补在末尾
        if (!layerOrder.empty())
        {
            std::vector<Cam::ToolpathJobInput::LayerBucket> ordered;
            ordered.reserve(input.layers.size());
            std::vector<bool> consumed(input.layers.size(), false);
            for (int lid : layerOrder)
            {
                for (size_t i = 0; i < input.layers.size(); ++i)
                {
                    if (!consumed[i] && input.layers[i].layerId == lid)
                    {
                        ordered.push_back(std::move(input.layers[i]));
                        consumed[i] = true;
                        break;
                    }
                }
            }
            for (size_t i = 0; i < input.layers.size(); ++i)
            {
                if (!consumed[i])
                {
                    ordered.push_back(std::move(input.layers[i]));
                }
            }
            input.layers = std::move(ordered);
        }

        return input;
    }

}  // namespace CamBridge
