/**
 * @file RasterLaser.cpp
 * @brief 激光工艺专用位图处理：抖动/半调、去孤点、高度图、1 bit 打包。
 */

#include "Core/RasterInternal.h"

#include <algorithm>
#include <cmath>

namespace
{
    using raster::internal::setLastError;

    /** @brief 8x8 Bayer 有序抖动矩阵（取值 0..63）。 */
    constexpr int kBayer8x8[64] = {
         0, 32,  8, 40,  2, 34, 10, 42,
        48, 16, 56, 24, 50, 18, 58, 26,
        12, 44,  4, 36, 14, 46,  6, 38,
        60, 28, 52, 20, 62, 30, 54, 22,
         3, 35, 11, 43,  1, 33,  9, 41,
        51, 19, 59, 27, 49, 17, 57, 25,
        15, 47,  7, 39, 13, 45,  5, 37,
        63, 31, 55, 23, 61, 29, 53, 21
    };


    /** @brief 误差扩散抖动的公共实现。 */
    void errorDiffuse(cv::Mat& gray, bool atkinson)
    {
        const int rows = gray.rows;
        const int cols = gray.cols;
        for (int y = 0; y < rows; ++y) {
            for (int x = 0; x < cols; ++x) {
                const int oldValue = gray.at<uint8_t>(y, x);
                const int newValue = oldValue < 128 ? 0 : 255;
                gray.at<uint8_t>(y, x) = static_cast<uint8_t>(newValue);
                const int error = oldValue - newValue;

                if (atkinson) {
                    // Atkinson：只向 6 个邻居扩散 1/8 误差，阴影更干净。
                    const int share = error / 8;
                    const int offsets[6][2] = {{1, 0}, {2, 0}, {-1, 1}, {0, 1}, {1, 1}, {2, 1}};
                    for (const auto& offset : offsets) {
                        const int nx = x + offset[0];
                        const int ny = y + offset[1];
                        if (nx < 0 || nx >= cols || ny >= rows) {
                            continue;
                        }
                        // at<uint8_t>() 返回的是代理对象，不是 int&，必须取出值再写回。
                        const int current = gray.at<uint8_t>(ny, nx);
                        gray.at<uint8_t>(ny, nx) = static_cast<uint8_t>(std::clamp(current + share, 0, 255));
                    }
                } else {
                    // Floyd-Steinberg：向 4 个邻居各扩散 1/16 误差。
                    const int share = error / 16;
                    const int offsets[4][2] = {{1, 0}, {-1, 1}, {0, 1}, {1, 1}};
                    for (const auto& offset : offsets) {
                        const int nx = x + offset[0];
                        const int ny = y + offset[1];
                        if (nx < 0 || nx >= cols || ny >= rows) {
                            continue;
                        }
                        const int current = gray.at<uint8_t>(ny, nx);
                        gray.at<uint8_t>(ny, nx) = static_cast<uint8_t>(std::clamp(current + share, 0, 255));
                    }
                }
            }
        }
    }

    /** @brief 有序抖动（Bayer 8x8）。 */
    void orderedDither(cv::Mat& gray)
    {
        const int rows = gray.rows;
        const int cols = gray.cols;
        for (int y = 0; y < rows; ++y) {
            for (int x = 0; x < cols; ++x) {
                const int oldValue = gray.at<uint8_t>(y, x);
                const int threshold = kBayer8x8[(y % 8) * 8 + (x % 8)];
                const int level = oldValue * 64 / 256;
                gray.at<uint8_t>(y, x) = static_cast<uint8_t>(level > threshold ? 255 : 0);
            }
        }
    }

    /** @brief 网屏（半调）：按单元聚簇网点/线条，把连续灰度转成二值。 */
    cv::Mat screenHalftone(const cv::Mat& gray, int cellSize, double angleDeg, int method)
    {
        const cv::Point2f center(static_cast<float>(gray.cols) * 0.5f,
                                 static_cast<float>(gray.rows) * 0.5f);
        const cv::Mat rotation = cv::getRotationMatrix2D(center, angleDeg, 1.0);
        cv::Mat rotated;
        cv::warpAffine(gray, rotated, rotation, gray.size(), cv::INTER_LINEAR, cv::BORDER_CONSTANT,
                       cv::Scalar::all(255));

        cv::Mat canvas(rotated.size(), CV_8UC1, cv::Scalar::all(255));
        const double cell = static_cast<double>(cellSize);
        for (int top = 0; top < rotated.rows; top += cellSize) {
            for (int left = 0; left < rotated.cols; left += cellSize) {
                const int width = std::min(cellSize, rotated.cols - left);
                const int height = std::min(cellSize, rotated.rows - top);
                if (width <= 0 || height <= 0) {
                    continue;
                }
                const cv::Mat cellMat = rotated(cv::Rect(left, top, width, height));
                const double mean = cv::mean(cellMat)[0];
                // 调子：0 = 全白（不刻），1 = 全黑（全刻）。
                const double tone = std::clamp(1.0 - mean / 255.0, 0.0, 1.0);
                if (tone <= 0.0) {
                    continue;
                }
                const cv::Point centerPoint(left + width / 2, top + height / 2);

                switch (method) {
                    case RASTER_HALFTONE_LINE:
                    case RASTER_HALFTONE_CROSS: {
                        const int thickness = std::max(1, static_cast<int>(std::lround(tone * height)));
                        cv::rectangle(canvas, cv::Rect(left, centerPoint.y - thickness / 2, width, thickness),
                                      cv::Scalar(0), cv::FILLED);
                        if (method == RASTER_HALFTONE_CROSS) {
                            cv::rectangle(canvas, cv::Rect(centerPoint.x - thickness / 2, top, thickness, height),
                                          cv::Scalar(0), cv::FILLED);
                        }
                        break;
                    }
                    case RASTER_HALFTONE_DOT:
                    default: {
                        // 面积正比于调子 -> 半径正比于 sqrt(tone)
                        const double radius = 0.5 * cell * std::sqrt(tone);
                        if (radius >= 0.5) {
                            cv::circle(canvas, centerPoint, static_cast<int>(std::lround(radius)),
                                       cv::Scalar(0), cv::FILLED);
                        }
                        break;
                    }
                }
            }
        }

        cv::Mat result;
        const cv::Mat inverse = cv::getRotationMatrix2D(center, -angleDeg, 1.0);
        cv::warpAffine(canvas, result, inverse, gray.size(), cv::INTER_LINEAR, cv::BORDER_CONSTANT,
                       cv::Scalar::all(255));
        // 回转用了双线性插值，会在边缘引入灰度过渡。半调输出必须是**严格二值**
        // ——它最终要变成 1 bit 的雕刻图，任何中间灰度都会变成多余的烧穿点。
        cv::threshold(result, result, 127, 255, cv::THRESH_BINARY);
        return result;
    }
}  // namespace

extern "C" {

RasterStatus RASTER_CALL Raster_Dither(RasterImage src, int32_t method, RasterImage* out)
{
    if (out == nullptr) {
        setLastError("output handle pointer is null");
        return RASTER_E_INVALID_ARG;
    }
    *out = nullptr;
    if (!raster::internal::validImage(src)) {
        return RASTER_E_INVALID_ARG;
    }
    return raster::internal::guard("Raster_Dither", [&]() -> RasterStatus {
        // clone() 是必须的：asGray8 在输入已是 8 位灰度时返回的是共享数据的浅拷贝，
        // 而抖动算法会就地改写像素 —— 不 clone 就会把调用方的原图改掉。
        cv::Mat work = raster::internal::asGray8(src->mat).clone();
        switch (method) {
            case RASTER_DITHER_NONE: {
                cv::Mat result;
                cv::threshold(work, result, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
                return raster::internal::emitImage(result, out);
            }
            case RASTER_DITHER_FLOYD_STEINBERG:
                errorDiffuse(work, false);
                break;
            case RASTER_DITHER_ATKINSON:
                errorDiffuse(work, true);
                break;
            case RASTER_DITHER_BAYER_8X8:
                orderedDither(work);
                break;
            case RASTER_DITHER_CLUSTERED_DOT: {
                cv::Mat result = screenHalftone(work, 6, 45.0, RASTER_HALFTONE_DOT);
                return raster::internal::emitImage(result, out);
            }
            default:
                setLastError("unknown dither method: " + std::to_string(method));
                return RASTER_E_INVALID_ARG;
        }
        return raster::internal::emitImage(work, out);
    });
}

RasterStatus RASTER_CALL Raster_Halftone(RasterImage src, int32_t cellSize, double angleDeg,
                                         int32_t method, RasterImage* out)
{
    if (out == nullptr) {
        setLastError("output handle pointer is null");
        return RASTER_E_INVALID_ARG;
    }
    *out = nullptr;
    if (!raster::internal::validImage(src)) {
        return RASTER_E_INVALID_ARG;
    }
    if (cellSize < 2) {
        setLastError("cell size must be at least 2 pixels");
        return RASTER_E_INVALID_ARG;
    }
    return raster::internal::guard("Raster_Halftone", [&]() -> RasterStatus {
        const cv::Mat gray = raster::internal::asGray8(src->mat);
        const cv::Mat result = screenHalftone(gray, cellSize, angleDeg, method);
        return raster::internal::emitImage(result, out);
    });
}

RasterStatus RASTER_CALL Raster_Despeckle(RasterImage src, int32_t radius, int32_t minArea,
                                          RasterImage* out)
{
    if (out == nullptr) {
        setLastError("output handle pointer is null");
        return RASTER_E_INVALID_ARG;
    }
    *out = nullptr;
    if (!raster::internal::validImage(src)) {
        return RASTER_E_INVALID_ARG;
    }
    if (minArea < 0) {
        setLastError("minArea must not be negative");
        return RASTER_E_INVALID_ARG;
    }
    if (radius < 0) {
        setLastError("radius must not be negative");
        return RASTER_E_INVALID_ARG;
    }
    return raster::internal::guard("Raster_Despeckle", [&]() -> RasterStatus {
        // 同上：阈值化会就地改写，必须先 clone 断开与源图的数据共享。
        cv::Mat binary = raster::internal::asGray8(src->mat).clone();
        // 输入不是二值图时先做 Otsu，让"暗 = 待加工"的约定成立。
        const int nonZero = cv::countNonZero(binary);
        const bool alreadyBinary = (nonZero == 0) || (nonZero == static_cast<int>(binary.total()));
        if (!alreadyBinary) {
            cv::threshold(binary, binary, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
        }

        cv::Mat labels;
        cv::Mat stats;
        cv::Mat centroids;
        const int componentCount = cv::connectedComponentsWithStats(binary, labels, stats, centroids,
                                                                    8, CV_32S);

        // 先按连通域决定去留，得到一张"标签 -> 保留"的查表。
        std::vector<uint8_t> keep(static_cast<std::size_t>(componentCount), 1);
        for (int label = 1; label < componentCount; ++label) {
            const int area = stats.at<int>(label, cv::CC_STAT_AREA);
            const int width = stats.at<int>(label, cv::CC_STAT_WIDTH);
            const int height = stats.at<int>(label, cv::CC_STAT_HEIGHT);
            const int longest = std::max(width, height);
            const bool tooSmall = area < minArea;
            const bool tooThin = radius > 0 && longest < radius;
            if (tooSmall || tooThin) {
                keep[static_cast<std::size_t>(label)] = 0;
            }
        }

        // 再**单趟扫描**按查表清像素。
        //
        // 早先的写法是"每个连通域做一次全图比较 (labels == label) + 掩码填充"，
        // 复杂度是 O(连通域数 × 像素数)。在 3 MPix、上万连通域的图上实测要
        // 40 秒 —— 一次去孤点把整条工艺流程拖成了 85 秒。
        // 改成查表 + 单趟扫描后是 O(像素数)，快三个数量级。
        cv::Mat cleaned = binary.clone();
        for (int y = 0; y < binary.rows; ++y) {
            const int32_t* labelRow = labels.ptr<int32_t>(y);
            uint8_t* outRow = cleaned.ptr<uint8_t>(y);
            for (int x = 0; x < binary.cols; ++x) {
                const int32_t label = labelRow[x];
                if (label >= 0 && label < componentCount && keep[static_cast<std::size_t>(label)] == 0) {
                    outRow[x] = 0;
                }
            }
        }
        return raster::internal::emitImage(cleaned, out);
    });
}

RasterStatus RASTER_CALL Raster_HeightMapFromGray(RasterImage src, double minHeight,
                                                  double maxHeight, RasterImage* out)
{
    if (out == nullptr) {
        setLastError("output handle pointer is null");
        return RASTER_E_INVALID_ARG;
    }
    *out = nullptr;
    if (!raster::internal::validImage(src)) {
        return RASTER_E_INVALID_ARG;
    }
    if (minHeight < 0.0 || maxHeight > 1.0 || minHeight > maxHeight) {
        setLastError("height range must satisfy 0 <= minHeight <= maxHeight <= 1");
        return RASTER_E_INVALID_ARG;
    }
    return raster::internal::guard("Raster_HeightMapFromGray", [&]() -> RasterStatus {
        const cv::Mat gray = raster::internal::asGray8(src->mat);
        const double low = minHeight * 65535.0;
        const double high = maxHeight * 65535.0;
        cv::Mat height;
        // 归一化到 [low, high]：灰度 0 -> 最低高度，灰度 255 -> 最高高度。
        gray.convertTo(height, CV_16U, (high - low) / 255.0, low);
        return raster::internal::emitImage(height, out);
    });
}

RasterStatus RASTER_CALL Raster_PackToOneBit(RasterImage src, uint8_t* buffer, size_t bufferSize,
                                             size_t* written, int32_t* strideBytes)
{
    if (buffer == nullptr) {
        setLastError("output buffer is null");
        return RASTER_E_INVALID_ARG;
    }
    if (!raster::internal::validImage(src)) {
        return RASTER_E_INVALID_ARG;
    }
    const cv::Mat gray = raster::internal::asGray8(src->mat);
    const std::size_t stride = (static_cast<std::size_t>(gray.cols) + 7) / 8;
    const std::size_t required = stride * static_cast<std::size_t>(gray.rows);
    if (bufferSize < required) {
        setLastError("output buffer is too small: need " + std::to_string(required) +
                     " bytes, got " + std::to_string(bufferSize));
        return RASTER_E_BUFFER_TOO_SMALL;
    }
    std::memset(buffer, 0, required);
    for (int y = 0; y < gray.rows; ++y) {
        const uint8_t* row = gray.ptr<uint8_t>(y);
        uint8_t* outRow = buffer + stride * static_cast<std::size_t>(y);
        for (int x = 0; x < gray.cols; ++x) {
            if (row[x] > 127) {
                outRow[x >> 3] |= static_cast<uint8_t>(0x80u >> (x & 7));
            }
        }
    }
    if (written != nullptr) {
        *written = required;
    }
    if (strideBytes != nullptr) {
        *strideBytes = static_cast<int32_t>(stride);
    }
    return RASTER_OK;
}

}  // extern "C"
