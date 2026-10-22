/**
 * @file RasterImage.cpp
 * @brief 图像句柄的创建、拷贝、查询与释放。
 */

#include "Core/RasterInternal.h"

namespace
{
    using raster::internal::channelCount;
    using raster::internal::setLastError;
    using raster::internal::toCvType;

    /**
     * @brief 校验视图参数并算出期望的数据长度。
     * @param stride 视图声明的行跨度（0 = 紧凑）
     * @return 期望字节数；参数非法返回 0
     */
    std::size_t validateView(const RasterImageView& view, std::size_t& strideOut)
    {
        if (view.width <= 0 || view.height <= 0) {
            setLastError("image view has non-positive dimensions");
            return 0;
        }
        if (view.data == nullptr) {
            setLastError("image view has a null pixel pointer");
            return 0;
        }
        const RasterPixelFormat format = static_cast<RasterPixelFormat>(view.format);
        const int channels = channelCount(format);
        if (channels == 0) {
            setLastError("image view has an unknown pixel format");
            return 0;
        }
        const std::size_t rowBytes =
            static_cast<std::size_t>(view.width) * static_cast<std::size_t>(channels);
        const std::size_t minStride = rowBytes;
        const std::size_t stride = view.stride > 0 ? static_cast<std::size_t>(view.stride) : minStride;
        if (stride < minStride) {
            setLastError("image view stride is smaller than one packed row");
            return 0;
        }
        strideOut = stride;
        return stride * static_cast<std::size_t>(view.height - 1) + rowBytes;
    }
}  // namespace

extern "C" {

RasterStatus RASTER_CALL Raster_CreateImage(int32_t width, int32_t height, int32_t format,
                                             RasterImage* out)
{
    if (out == nullptr) {
        setLastError("output handle pointer is null");
        return RASTER_E_INVALID_ARG;
    }
    *out = nullptr;
    if (width <= 0 || height <= 0) {
        setLastError("image dimensions must be positive");
        return RASTER_E_INVALID_ARG;
    }
    const RasterPixelFormat pixelFormat = static_cast<RasterPixelFormat>(format);
    const int cvType = toCvType(pixelFormat);
    if (cvType < 0) {
        setLastError("unknown pixel format");
        return RASTER_E_INVALID_ARG;
    }
    // 尺寸上限：OpenCV 的行跨度是 int，超过 2GB 会溢出。
    constexpr int64_t kMaxDimension = 1 << 20;
    if (width > kMaxDimension || height > kMaxDimension) {
        setLastError("image dimensions exceed the supported maximum");
        return RASTER_E_INVALID_ARG;
    }

    return raster::internal::guard("Raster_CreateImage", [&]() -> RasterStatus {
        cv::Mat mat(static_cast<int>(height), static_cast<int>(width), cvType, cv::Scalar::all(0));
        *out = raster::internal::makeImage(mat);
        return RASTER_OK;
    });
}

RasterStatus RASTER_CALL Raster_CreateImageFromView(const RasterImageView* view, RasterImage* out)
{
    if (out == nullptr) {
        setLastError("output handle pointer is null");
        return RASTER_E_INVALID_ARG;
    }
    *out = nullptr;
    if (!raster::internal::checkStructSize(view)) {
        return RASTER_E_STRUCT_SIZE;
    }
    std::size_t stride = 0;
    const std::size_t required = validateView(*view, stride);
    if (required == 0) {
        return RASTER_E_INVALID_ARG;
    }

    const RasterPixelFormat pixelFormat = static_cast<RasterPixelFormat>(view->format);
    const int channels = channelCount(pixelFormat);
    const int bytesPerPixel = raster::internal::bytesPerChannel(pixelFormat) * channels;
    const std::size_t rowBytes = static_cast<std::size_t>(view->width) * static_cast<std::size_t>(bytesPerPixel);
    const bool tightlyPacked = (stride == rowBytes);

    return raster::internal::guard("Raster_CreateImageFromView", [&]() -> RasterStatus {
        cv::Mat source(static_cast<int>(view->height), static_cast<int>(view->width), toCvType(pixelFormat),
                       const_cast<void*>(view->data), static_cast<std::size_t>(stride));
        cv::Mat copy;
        if (tightlyPacked) {
            copy = source.clone();
        } else {
            // 外部内存可能有行对齐填充，逐行拷贝。
            copy.create(source.rows, source.cols, source.type());
            for (int row = 0; row < source.rows; ++row) {
                std::memcpy(copy.ptr(row), source.ptr(row), rowBytes);
            }
        }
        *out = raster::internal::makeImage(copy);
        return RASTER_OK;
    });
}

RasterStatus RASTER_CALL Raster_CloneImage(RasterImage src, RasterImage* out)
{
    if (out == nullptr) {
        setLastError("output handle pointer is null");
        return RASTER_E_INVALID_ARG;
    }
    *out = nullptr;
    if (!raster::internal::validImage(src)) {
        return RASTER_E_INVALID_ARG;
    }
    return raster::internal::guard("Raster_CloneImage", [&]() -> RasterStatus {
        *out = raster::internal::makeImage(src->mat.clone());
        return RASTER_OK;
    });
}

RasterStatus RASTER_CALL Raster_GetImageInfo(RasterImage img, RasterImageInfo* out)
{
    if (!raster::internal::checkStructSize(out)) {
        return RASTER_E_STRUCT_SIZE;
    }
    if (!raster::internal::validImage(img)) {
        return RASTER_E_INVALID_ARG;
    }
    const cv::Mat& mat = img->mat;
    out->width = mat.cols;
    out->height = mat.rows;
    out->channels = mat.channels();
    out->format = static_cast<int32_t>(raster::internal::fromCvType(mat.type()));
    out->dataSize = static_cast<std::size_t>(mat.step[0]) * static_cast<std::size_t>(mat.rows);
    return RASTER_OK;
}

RasterStatus RASTER_CALL Raster_GetImageView(RasterImage img, RasterImageView* out)
{
    if (!raster::internal::checkStructSize(out)) {
        return RASTER_E_STRUCT_SIZE;
    }
    if (!raster::internal::validImage(img)) {
        return RASTER_E_INVALID_ARG;
    }
    const cv::Mat& mat = img->mat;
    out->width = mat.cols;
    out->height = mat.rows;
    out->channels = mat.channels();
    out->format = static_cast<int32_t>(raster::internal::fromCvType(mat.type()));
    out->data = mat.data;
    out->stride = static_cast<int32_t>(mat.step[0]);
    return RASTER_OK;
}

RasterStatus RASTER_CALL Raster_CopyImageTo(RasterImage img, void* dst, size_t dstSize,
                                            int32_t dstStride)
{
    if (dst == nullptr) {
        setLastError("destination buffer is null");
        return RASTER_E_INVALID_ARG;
    }
    if (!raster::internal::validImage(img)) {
        return RASTER_E_INVALID_ARG;
    }
    const cv::Mat& mat = img->mat;
    const std::size_t bytesPerPixel =
        static_cast<std::size_t>(raster::internal::bytesPerChannel(raster::internal::fromCvType(mat.type()))) *
        static_cast<std::size_t>(mat.channels());
    const std::size_t rowBytes = static_cast<std::size_t>(mat.cols) * bytesPerPixel;
    const std::size_t stride =
        dstStride > 0 ? static_cast<std::size_t>(dstStride) : rowBytes;
    if (stride < rowBytes) {
        setLastError("destination stride is smaller than one packed row");
        return RASTER_E_INVALID_ARG;
    }
    const std::size_t required = stride * static_cast<std::size_t>(mat.rows - 1) + rowBytes;
    if (dstSize < required) {
        setLastError("destination buffer is too small: need " + std::to_string(required) +
                     " bytes, got " + std::to_string(dstSize));
        return RASTER_E_BUFFER_TOO_SMALL;
    }
    for (int row = 0; row < mat.rows; ++row) {
        std::memcpy(static_cast<uint8_t*>(dst) + stride * static_cast<std::size_t>(row), mat.ptr(row),
                    rowBytes);
    }
    return RASTER_OK;
}

void RASTER_CALL Raster_DestroyImage(RasterImage img)
{
    delete img;
}

}  // extern "C"
