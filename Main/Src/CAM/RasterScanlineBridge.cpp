/**
 * @file RasterScanlineBridge.cpp
 * @brief 位图实体 → 扫描线段的实现（毫米进、毫米出）。
 */

#include "RasterScanlineBridge.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#ifdef BUILD_RASTER
#include "Raster/RasterApi.h"
#include "Raster/RasterTypes.h"
#endif

namespace
{
    /** @brief 无像素当量时的兜底像素当量：96 DPI（导入侧同样以此兜底） */
    constexpr double kFallbackPitchMm = 25.4 / 96.0;

    /// 像素坐标 → 用户坐标（双线性四点映射，兼容旋转/斜切）
    Ut::Vec2d mapImagePixel(const Eg::AprImage& image, double u, double v)
    {
        const double fu = image.nWidth > 0 ? u / static_cast<double>(image.nWidth) : 0.0;
        const double fv = image.nHeight > 0 ? v / static_cast<double>(image.nHeight) : 0.0;
        const Ut::Vec2d axisU = image.topRight - image.topLeft;
        const Ut::Vec2d axisV = image.bottomLeft - image.topLeft;
        return Ut::Vec2d(image.topLeft.x() + axisU.x() * fu + axisV.x() * fv,
                         image.topLeft.y() + axisU.y() * fu + axisV.y() * fv);
    }

    /// 像素格式对应的每像素字节数；未知格式返回 0
    size_t bytesPerPixelOf(Eg::AprPixelFormat format)
    {
        switch (format)
        {
        case Eg::AprPixelFormat::Gray8:
            return 1;
        case Eg::AprPixelFormat::RGB24:
            return 3;
        case Eg::AprPixelFormat::RGBA32:
            return 4;
        default:
            return 0;
        }
    }

    /// AprImage 像素 → 8 位灰度：RGB24 按 R,G,B 解释，RGBA32 先按白底合成 alpha
    bool imageToGray8(const Eg::AprImage& image, std::vector<uint8_t>& gray)
    {
        gray.clear();
        const size_t pixelCount =
            static_cast<size_t>(image.nWidth) * static_cast<size_t>(image.nHeight);
        const size_t expectedBpp = bytesPerPixelOf(image.ePixelFormat);
        if (pixelCount == 0 || expectedBpp == 0)
        {
            return false;
        }
        // 缓冲长度必须与「像素数 × 格式字节数」严格相等：
        // 只检查 >= 像素数会让 RGB24/RGBA32 的截断缓冲通过校验，随后逐像素读
        // p[1]/p[2]/p[3] 越界；长度不是整数倍时还会把相邻像素当成通道解释。
        const size_t dataSize = image.pixelDataSize();
        if (dataSize != pixelCount * expectedBpp)
        {
            return false;
        }
        const uint8_t* src = image.pixelData();
        if (src == nullptr)
        {
            return false;
        }
        const size_t bytesPerPixel = expectedBpp;

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
            case Eg::AprPixelFormat::Gray8:
                gray[i] = p[0];
                break;
            case Eg::AprPixelFormat::RGB24:
                gray[i] = clampByte(weightR * p[0] + weightG * p[1] + weightB * p[2]);
                break;
            case Eg::AprPixelFormat::RGBA32:
            {
                // 透明区域按白纸处理；极性为"暗 = 加工"，白（255）即不出光
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

#ifdef BUILD_RASTER
    /** @brief 像素当量（mm/px），横向与纵向分别给出 */
    struct PixelPitch
    {
        double x = kFallbackPitchMm;   ///< 沿行方向（列间距），用于过扫
        double y = kFallbackPitchMm;   ///< 跨行方向（行间距），用于线距
    };

    /**
     * @brief 取像素当量（mm/px）
     *
     * 行距是相邻扫描带之间的距离，沿图像纵向展开，必须用纵向当量；
     * 过扫是行首尾的额外行程，沿图像横向展开，必须用横向当量。
     * 旧实现两者共用「优先 X」的单一当量，各向异性图（X/Y 缩放不同）上
     * 行距会被放大 pitchY/pitchX 倍，实际刻线严重稀疏。
     *
     * 各向同性（相对差 < 1e-6）时纵向直接取横向值，保证既有输出逐位不变。
     */
    PixelPitch pixelPitchOf(const Eg::AprImage& image)
    {
        PixelPitch pitch;
        const double pitchX = image.pixelPitchMmX();
        const double pitchY = image.pixelPitchMmY();

        if (pitchX > 0.0 && pitchY > 0.0)
        {
            const double scale = std::max(std::fabs(pitchX), std::fabs(pitchY));
            if (std::fabs(pitchX - pitchY) <= 1e-6 * scale)
            {
                // 各向同性：两个方向等价，统一用横向值
                pitch.x = pitchX;
                pitch.y = pitchX;
                return pitch;
            }
        }

        pitch.x = pitchX > 0.0 ? pitchX : (pitchY > 0.0 ? pitchY : kFallbackPitchMm);
        pitch.y = pitchY > 0.0 ? pitchY : pitch.x;
        return pitch;
    }
#endif
}  // namespace

namespace CamBridge
{
    bool buildRasterRuns(const Eg::AprImage& image,
                         const RasterScanlineRequest& request,
                         std::vector<RasterRun>& out,
                         std::string& defect)
    {
        out.clear();
        defect.clear();

        if (image.nWidth <= 0 || image.nHeight <= 0)
        {
            defect = "raster image has zero size";
            return false;
        }

        std::vector<uint8_t> gray;
        if (!imageToGray8(image, gray))
        {
            defect = "raster pixel buffer is empty, truncated, or has unsupported format";
            return false;
        }

#ifdef BUILD_RASTER
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
            defect = Raster_StatusText(createStatus);
            return false;
        }

        const PixelPitch pitch = pixelPitchOf(image);
        const double spacingMm = request.lineSpacingMm > 0.0 ? request.lineSpacingMm : 0.1;

        RasterScanlineParams params{};
        params.structSize = sizeof(RasterScanlineParams);
        // 毫米 → 像素：只用真实像素当量，不在这里退回整像素。
        // 行距沿纵向（跨行），过扫沿横向（行首尾），各用各自方向的当量；
        // 各向同性图两者相等，因此输出与旧实现完全一致。
        params.lineSpacing = spacingMm / pitch.y;
        params.angleDeg = request.angleDeg;
        params.passes = request.passes > 0 ? request.passes : 1;
        params.speed = request.speedMmPerSec;
        params.powerMin = 0.0;
        params.powerMax = std::max(0.0, std::min(1.0, request.powerPercent / 100.0));
        params.bidirectional = request.bidirectional ? 1 : 0;
        params.overscan = request.overscanMm > 0.0 ? request.overscanMm / pitch.x : 0.0;
        // 仅上下文：Raster 库不据此换算任何坐标。各向异性图没有单一当量，这里给横向值。
        params.pixelPitchMm = pitch.x;

        RasterJobList jobs = nullptr;
        const RasterStatus status = Raster_ScanlineJobs(rasterImage, &params, &jobs);
        Raster_DestroyImage(rasterImage);
        if (status != RASTER_OK || jobs == nullptr)
        {
            defect = Raster_StatusText(status);
            return false;
        }

        int32_t count = 0;
        const RasterStatus countStatus = Raster_JobListCount(jobs, &count);
        if (countStatus != RASTER_OK || count < 0)
        {
            defect = std::string("Raster_JobListCount failed: ") + Raster_StatusText(countStatus);
            Raster_DestroyJobList(jobs);
            return false;
        }
        out.reserve(static_cast<size_t>(count));
        int32_t lineIndex = -1;
        double lastY = 0.0;
        bool hasLastY = false;
        for (int32_t i = 0; i < count; ++i)
        {
            RasterScanlineSegment seg{};
            const RasterStatus getStatus = Raster_JobListGet(jobs, i, &seg);
            if (getStatus != RASTER_OK)
            {
                // 读到一半失败时绝不能把已读部分当成功返回：
                // 截断的刀路看起来能跑，实际上会漏掉图像后半部分。
                out.clear();
                defect = std::string("Raster_JobListGet failed at segment ")
                    + std::to_string(i) + ": " + Raster_StatusText(getStatus);
                Raster_DestroyJobList(jobs);
                return false;
            }
            // 扫描带序号：y 变化即视为换带（整图分遍时换遍也会回到第一行）
            if (!hasLastY || std::fabs(seg.y - lastY) > 1e-9)
            {
                ++lineIndex;
                lastY = seg.y;
                hasLastY = true;
            }
            RasterRun run;
            run.line = lineIndex;
            run.start = mapImagePixel(image, seg.x0, seg.y);
            run.end = mapImagePixel(image, seg.x1, seg.y1);
            run.powerPercent = seg.laserOn != 0 ? seg.power * 100.0 : 0.0;
            run.speedMmPerSec = seg.speed;
            run.pass = seg.pass;
            run.dir = seg.dir;
            run.laserOn = seg.laserOn != 0;
            // 只有越出图像左右边界的段才是过扫段（库把过扫输出为关光段）。
            // 双向扫描时同一段会以反向端点给出（x0 > x1），因此按区间而不是按端点顺序判断，
            // 否则反向行的过扫段会被漏判，计划里就看不到那半边的过扫行程。
            const double runMinX = std::min(seg.x0, seg.x1);
            const double runMaxX = std::max(seg.x0, seg.x1);
            run.overscan = runMinX < 0.0 || runMaxX > static_cast<double>(view.width);
            out.push_back(run);
        }
        Raster_DestroyJobList(jobs);

        if (out.empty())
        {
            defect = "raster scanline job produced no segments";
            return false;
        }
        return true;
#else
        (void)request;
        defect = "raster support is not built (BUILD_RASTER=OFF)";
        return false;
#endif
    }
}  // namespace CamBridge
