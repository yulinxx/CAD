/**
 * @file RasterIO.cpp
 * @brief 图像文件读写。
 */

#include "Core/RasterInternal.h"

namespace
{
    using raster::internal::setLastError;

    /** @brief 把 OpenCV 读回来的任意类型收敛到本模块支持的像素格式。 */
    cv::Mat normalizeLoaded(const cv::Mat& src)
    {
        switch (src.type()) {
            case CV_8UC1:
            case CV_8UC3:
            case CV_8UC4:
            case CV_16UC1:
                return src;
            default:
                break;
        }
        if (src.depth() == CV_16U) {
            if (src.channels() == 1) {
                return src;
            }
            cv::Mat converted;
            src.convertTo(converted, CV_8U, 1.0 / 257.0);
            return converted;
        }
        if (src.channels() >= 3) {
            cv::Mat converted;
            src.convertTo(converted, CV_8U);
            return converted;
        }
        cv::Mat converted;
        src.convertTo(converted, CV_8U);
        return converted;
    }
}  // namespace

extern "C" {

RasterStatus RASTER_CALL Raster_LoadImage(const char* path, RasterImage* out)
{
    if (out == nullptr) {
        setLastError("output handle pointer is null");
        return RASTER_E_INVALID_ARG;
    }
    *out = nullptr;
    if (path == nullptr || *path == '\0') {
        setLastError("image path is null or empty");
        return RASTER_E_INVALID_ARG;
    }
    const std::string pathStr(path);

    return raster::internal::guard("Raster_LoadImage", [&]() -> RasterStatus {
        // IMREAD_UNCHANGED：保留 16 位灰度（高度图）与 alpha 通道。
        // 归一化在 normalizeLoaded() 里做，避免这里丢信息。
        const cv::Mat raw = cv::imread(pathStr, cv::IMREAD_UNCHANGED);
        if (raw.empty()) {
            setLastError("failed to read image: " + pathStr);
            return RASTER_E_IO;
        }
        *out = raster::internal::makeImage(normalizeLoaded(raw));
        return RASTER_OK;
    });
}

RasterStatus RASTER_CALL Raster_SaveImage(RasterImage img, const char* path)
{
    if (path == nullptr || *path == '\0') {
        setLastError("image path is null or empty");
        return RASTER_E_INVALID_ARG;
    }
    if (!raster::internal::validImage(img)) {
        return RASTER_E_INVALID_ARG;
    }
    const std::string pathStr(path);

    return raster::internal::guard("Raster_SaveImage", [&]() -> RasterStatus {
        if (!cv::imwrite(pathStr, img->mat)) {
            setLastError("failed to write image: " + pathStr);
            return RASTER_E_IO;
        }
        return RASTER_OK;
    });
}

RasterStatus RASTER_CALL Raster_SaveImageAs(RasterImage img, const char* path, int32_t format,
                                            int32_t quality)
{
    if (path == nullptr || *path == '\0') {
        setLastError("image path is null or empty");
        return RASTER_E_INVALID_ARG;
    }
    if (!raster::internal::validImage(img)) {
        return RASTER_E_INVALID_ARG;
    }
    const std::string pathStr(path);

    std::vector<int> params;
    switch (format) {
        case RASTER_FILE_JPEG:
        case RASTER_FILE_WEBP:
            if (quality > 0) {
                params.push_back(cv::IMWRITE_JPEG_QUALITY);
                params.push_back(quality > 100 ? 100 : quality);
            }
            break;
        case RASTER_FILE_PNG:
            // PNG 无损：压缩级别只影响文件大小，不改像素。
            params.push_back(cv::IMWRITE_PNG_COMPRESSION);
            params.push_back(6);
            break;
        default:
            break;
    }

    return raster::internal::guard("Raster_SaveImageAs", [&]() -> RasterStatus {
        const bool ok = params.empty() ? cv::imwrite(pathStr, img->mat)
                                       : cv::imwrite(pathStr, img->mat, params);
        if (!ok) {
            setLastError("failed to write image: " + pathStr +
                         " (format=" + std::to_string(format) + ")");
            return RASTER_E_IO;
        }
        return RASTER_OK;
    });
}

}  // extern "C"
