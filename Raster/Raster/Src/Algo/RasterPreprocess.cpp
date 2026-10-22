/**
 * @file RasterPreprocess.cpp
 * @brief 通用位图预处理：色彩空间、亮度对比度、滤波、阈值、几何变换、校正。
 */

#include "Core/RasterInternal.h"

#include <algorithm>
#include <cmath>

namespace
{
    using raster::internal::setLastError;


    /** @brief 把参数规整成 >=1 的奇数（形态学核尺寸要求奇数）。 */
    int toOddAtLeast(int value, int minimum)
    {
        if (value < minimum) {
            value = minimum;
        }
        if ((value & 1) == 0) {
            ++value;
        }
        return value;
    }
}  // namespace

extern "C" {

RasterStatus RASTER_CALL Raster_ToGray(RasterImage src, RasterImage* out)
{
    if (out == nullptr) {
        setLastError("output handle pointer is null");
        return RASTER_E_INVALID_ARG;
    }
    *out = nullptr;
    if (!raster::internal::validImage(src)) {
        return RASTER_E_INVALID_ARG;
    }
    const cv::Mat& mat = src->mat;
    return raster::internal::guard("Raster_ToGray", [&]() -> RasterStatus {
        if (mat.channels() == 1) {
            return raster::internal::emitImage(mat.clone(), out);
        }
        cv::Mat gray;
        if (mat.depth() == CV_16U) {
            // 16 位多通道罕见：先降到 8 位再转灰度，避免 cvtColor 类型不支持。
            cv::Mat reduced;
            mat.convertTo(reduced, CV_8U, 1.0 / 257.0);
            cv::cvtColor(reduced, gray, cv::COLOR_BGR2GRAY);
        } else {
            cv::cvtColor(mat, gray, mat.channels() == 4 ? cv::COLOR_BGRA2GRAY : cv::COLOR_BGR2GRAY);
        }
        return raster::internal::emitImage(gray, out);
    });
}

RasterStatus RASTER_CALL Raster_Invert(RasterImage src, RasterImage* out)
{
    if (out == nullptr) {
        setLastError("output handle pointer is null");
        return RASTER_E_INVALID_ARG;
    }
    *out = nullptr;
    if (!raster::internal::validImage(src)) {
        return RASTER_E_INVALID_ARG;
    }
    return raster::internal::guard("Raster_Invert", [&]() -> RasterStatus {
        cv::Mat inverted;
        cv::bitwise_not(src->mat, inverted);
        return raster::internal::emitImage(inverted, out);
    });
}

RasterStatus RASTER_CALL Raster_AdjustBrightnessContrast(RasterImage src, double alpha, double beta,
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
    if (!(alpha == alpha) || !(beta == beta)) {  // NaN 检查
        setLastError("alpha and beta must be finite numbers");
        return RASTER_E_INVALID_ARG;
    }
    return raster::internal::guard("Raster_AdjustBrightnessContrast", [&]() -> RasterStatus {
        cv::Mat adjusted;
        src->mat.convertTo(adjusted, -1, alpha, beta);
        return raster::internal::emitImage(adjusted, out);
    });
}

RasterStatus RASTER_CALL Raster_EqualizeHist(RasterImage src, RasterImage* out)
{
    if (out == nullptr) {
        setLastError("output handle pointer is null");
        return RASTER_E_INVALID_ARG;
    }
    *out = nullptr;
    if (!raster::internal::validImage(src)) {
        return RASTER_E_INVALID_ARG;
    }
    return raster::internal::guard("Raster_EqualizeHist", [&]() -> RasterStatus {
        const cv::Mat gray = raster::internal::asGray8(src->mat);
        cv::Mat equalized;
        cv::equalizeHist(gray, equalized);
        return raster::internal::emitImage(equalized, out);
    });
}

RasterStatus RASTER_CALL Raster_Clahe(RasterImage src, double clipLimit, int32_t gridSize,
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
    if (clipLimit <= 0.0) {
        setLastError("clipLimit must be greater than zero");
        return RASTER_E_INVALID_ARG;
    }
    if (gridSize < 1) {
        setLastError("gridSize must be at least 1");
        return RASTER_E_INVALID_ARG;
    }
    return raster::internal::guard("Raster_Clahe", [&]() -> RasterStatus {
        const cv::Mat gray = raster::internal::asGray8(src->mat);
        cv::Ptr<cv::CLAHE> clahe = cv::createCLAHE(clipLimit, cv::Size(gridSize, gridSize));
        cv::Mat enhanced;
        clahe->apply(gray, enhanced);
        return raster::internal::emitImage(enhanced, out);
    });
}

RasterStatus RASTER_CALL Raster_Denoise(RasterImage src, int32_t method, double strength,
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
    return raster::internal::guard("Raster_Denoise", [&]() -> RasterStatus {
        const cv::Mat& mat = src->mat;
        cv::Mat result;
        switch (method) {
            case RASTER_DENOISE_FAST_NLMEANS: {
                const double h = strength > 0.0 ? strength : 10.0;
                if (mat.channels() >= 3 && mat.depth() == CV_8U) {
                    cv::fastNlMeansDenoisingColored(mat, result, h, 7, 21);
                } else {
                    cv::fastNlMeansDenoising(raster::internal::asGray8(mat), result, h, 7, 21);
                }
                break;
            }
            case RASTER_DENOISE_BILATERAL: {
                const int diameter = toOddAtLeast(static_cast<int>(strength), 3);
                const double sigmaColor = 50.0;
                const double sigmaSpace = 50.0;
                cv::bilateralFilter(mat, result, diameter, sigmaColor, sigmaSpace);
                break;
            }
            case RASTER_DENOISE_MEDIAN: {
                const int ksize = toOddAtLeast(static_cast<int>(strength), 3);
                cv::medianBlur(raster::internal::asGray8(mat), result, ksize);
                break;
            }
            case RASTER_DENOISE_GAUSSIAN: {
                const int ksize = toOddAtLeast(static_cast<int>(strength), 3);
                cv::GaussianBlur(mat, result, cv::Size(ksize, ksize), 0.0, 0.0);
                break;
            }
            default:
                setLastError("unknown denoise method: " + std::to_string(method));
                return RASTER_E_INVALID_ARG;
        }
        return raster::internal::emitImage(result, out);
    });
}

RasterStatus RASTER_CALL Raster_Sharpen(RasterImage src, double amount, RasterImage* out)
{
    if (out == nullptr) {
        setLastError("output handle pointer is null");
        return RASTER_E_INVALID_ARG;
    }
    *out = nullptr;
    if (!raster::internal::validImage(src)) {
        return RASTER_E_INVALID_ARG;
    }
    if (amount < 0.0) {
        setLastError("sharpen amount must not be negative");
        return RASTER_E_INVALID_ARG;
    }
    if (amount == 0.0) {
        return raster::internal::emitImage(src->mat.clone(), out);
    }
    return raster::internal::guard("Raster_Sharpen", [&]() -> RasterStatus {
        const cv::Mat& mat = src->mat;
        const double radius = 1.5;
        cv::Mat blurred;
        cv::GaussianBlur(mat, blurred, cv::Size(), radius, radius);
        // 反锐化掩模：out = (1 + amount) * src - amount * blurred
        cv::Mat sharpened;
        cv::addWeighted(mat, 1.0 + amount, blurred, -amount, 0.0, sharpened);
        return raster::internal::emitImage(sharpened, out);
    });
}

RasterStatus RASTER_CALL Raster_Threshold(RasterImage src, int32_t method, double thresh,
                                          double maxValue, RasterImage* out)
{
    if (out == nullptr) {
        setLastError("output handle pointer is null");
        return RASTER_E_INVALID_ARG;
    }
    *out = nullptr;
    if (!raster::internal::validImage(src)) {
        return RASTER_E_INVALID_ARG;
    }
    return raster::internal::guard("Raster_Threshold", [&]() -> RasterStatus {
        const cv::Mat gray = raster::internal::asGray8(src->mat);
        cv::Mat result;
        switch (method) {
            case RASTER_THRESH_BINARY: {
                const double hi = (maxValue > 0.0 && maxValue <= 255.0) ? maxValue : 255.0;
                cv::threshold(gray, result, thresh, hi, cv::THRESH_BINARY);
                break;
            }
            case RASTER_THRESH_OTSU: {
                const double hi = (maxValue > 0.0 && maxValue <= 255.0) ? maxValue : 255.0;
                cv::threshold(gray, result, 0.0, hi, cv::THRESH_BINARY | cv::THRESH_OTSU);
                break;
            }
            case RASTER_THRESH_ADAPTIVE: {
                // thresh 复用为 blockSize，maxValue 复用为 C 偏移量。
                const int blockSize = toOddAtLeast(static_cast<int>(thresh), 3);
                const double c = maxValue;
                cv::adaptiveThreshold(gray, result, 255.0, cv::ADAPTIVE_THRESH_GAUSSIAN_C,
                                      cv::THRESH_BINARY, blockSize, c);
                break;
            }
            case RASTER_THRESH_TRIANGLE: {
                const double hi = (maxValue > 0.0 && maxValue <= 255.0) ? maxValue : 255.0;
                cv::threshold(gray, result, thresh, hi, cv::THRESH_BINARY | cv::THRESH_TRIANGLE);
                break;
            }
            case RASTER_THRESH_TRUNC: {
                const double hi = (maxValue > 0.0 && maxValue <= 255.0) ? maxValue : 255.0;
                cv::threshold(gray, result, thresh, hi, cv::THRESH_TRUNC);
                break;
            }
            case RASTER_THRESH_TOZERO: {
                const double hi = (maxValue > 0.0 && maxValue <= 255.0) ? maxValue : 255.0;
                cv::threshold(gray, result, thresh, hi, cv::THRESH_TOZERO);
                break;
            }
            default:
                setLastError("unknown threshold method: " + std::to_string(method));
                return RASTER_E_INVALID_ARG;
        }
        return raster::internal::emitImage(result, out);
    });
}

RasterStatus RASTER_CALL Raster_Morphology(RasterImage src, int32_t op, int32_t kernelShape,
                                           int32_t ksize, RasterImage* out)
{
    if (out == nullptr) {
        setLastError("output handle pointer is null");
        return RASTER_E_INVALID_ARG;
    }
    *out = nullptr;
    if (!raster::internal::validImage(src)) {
        return RASTER_E_INVALID_ARG;
    }
    if (ksize < 1) {
        setLastError("kernel size must be at least 1");
        return RASTER_E_INVALID_ARG;
    }
    return raster::internal::guard("Raster_Morphology", [&]() -> RasterStatus {
        const cv::Mat gray = raster::internal::asGray8(src->mat);
        const int size = toOddAtLeast(ksize, 1);
        const cv::Mat kernel = cv::getStructuringElement(
            raster::internal::toMorphShape(kernelShape), cv::Size(size, size));
        cv::Mat result;
        switch (op) {
            case RASTER_MORPH_ERODE:    cv::erode(gray, result, kernel); break;
            case RASTER_MORPH_DILATE:   cv::dilate(gray, result, kernel); break;
            case RASTER_MORPH_OPEN:     cv::morphologyEx(gray, result, cv::MORPH_OPEN, kernel); break;
            case RASTER_MORPH_CLOSE:    cv::morphologyEx(gray, result, cv::MORPH_CLOSE, kernel); break;
            case RASTER_MORPH_GRADIENT: cv::morphologyEx(gray, result, cv::MORPH_GRADIENT, kernel); break;
            case RASTER_MORPH_TOPHAT:   cv::morphologyEx(gray, result, cv::MORPH_TOPHAT, kernel); break;
            case RASTER_MORPH_BLACKHAT: cv::morphologyEx(gray, result, cv::MORPH_BLACKHAT, kernel); break;
            default:
                setLastError("unknown morphology op: " + std::to_string(op));
                return RASTER_E_INVALID_ARG;
        }
        return raster::internal::emitImage(result, out);
    });
}

RasterStatus RASTER_CALL Raster_Resize(RasterImage src, double fx, double fy, int32_t interpolation,
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
    if (fx <= 0.0 && fy <= 0.0) {
        setLastError("at least one of fx and fy must be greater than zero");
        return RASTER_E_INVALID_ARG;
    }
    return raster::internal::guard("Raster_Resize", [&]() -> RasterStatus {
        cv::Size target = src->mat.size();
        if (fx > 0.0 && fy > 0.0) {
            target = cv::Size(std::max(1, static_cast<int>(std::lround(src->mat.cols * fx))),
                              std::max(1, static_cast<int>(std::lround(src->mat.rows * fy))));
        } else if (fx > 0.0) {
            target = cv::Size(std::max(1, static_cast<int>(std::lround(src->mat.cols * fx))),
                              src->mat.rows);
        } else {
            target = cv::Size(src->mat.cols,
                              std::max(1, static_cast<int>(std::lround(src->mat.rows * fy))));
        }
        cv::Mat result;
        cv::resize(src->mat, result, target, 0, 0, raster::internal::toInterpolation(interpolation));
        return raster::internal::emitImage(result, out);
    });
}

RasterStatus RASTER_CALL Raster_Rotate(RasterImage src, double angleDeg, int32_t borderMode,
                                       double borderValue, RasterImage* out)
{
    if (out == nullptr) {
        setLastError("output handle pointer is null");
        return RASTER_E_INVALID_ARG;
    }
    *out = nullptr;
    if (!raster::internal::validImage(src)) {
        return RASTER_E_INVALID_ARG;
    }
    return raster::internal::guard("Raster_Rotate", [&]() -> RasterStatus {
        const cv::Mat& mat = src->mat;
        const cv::Point2f center(static_cast<float>(mat.cols) * 0.5f,
                                 static_cast<float>(mat.rows) * 0.5f);
        const cv::Mat rotation = cv::getRotationMatrix2D(center, angleDeg, 1.0);
        const cv::Scalar border(static_cast<double>(borderValue), static_cast<double>(borderValue),
                                static_cast<double>(borderValue), static_cast<double>(borderValue));
        cv::Mat result;
        cv::warpAffine(mat, result, rotation, mat.size(), cv::INTER_LINEAR,
                       raster::internal::toBorderMode(borderMode), border);
        return raster::internal::emitImage(result, out);
    });
}

RasterStatus RASTER_CALL Raster_Crop(RasterImage src, int32_t x, int32_t y, int32_t width,
                                      int32_t height, RasterImage* out)
{
    if (out == nullptr) {
        setLastError("output handle pointer is null");
        return RASTER_E_INVALID_ARG;
    }
    *out = nullptr;
    if (!raster::internal::validImage(src)) {
        return RASTER_E_INVALID_ARG;
    }
    if (width <= 0 || height <= 0) {
        setLastError("crop size must be positive");
        return RASTER_E_INVALID_ARG;
    }
    const cv::Rect requested(x, y, width, height);
    const cv::Rect clipped = requested & (cv::Rect(0, 0, src->mat.cols, src->mat.rows));
    if (clipped.width <= 0 || clipped.height <= 0) {
        setLastError("crop rectangle does not intersect the image");
        return RASTER_E_INVALID_ARG;
    }
    return raster::internal::guard("Raster_Crop", [&]() -> RasterStatus {
        return raster::internal::emitImage(src->mat(clipped).clone(), out);
    });
}

RasterStatus RASTER_CALL Raster_Flip(RasterImage src, int32_t axis, RasterImage* out)
{
    if (out == nullptr) {
        setLastError("output handle pointer is null");
        return RASTER_E_INVALID_ARG;
    }
    *out = nullptr;
    if (!raster::internal::validImage(src)) {
        return RASTER_E_INVALID_ARG;
    }
    int flipCode = 1;
    switch (axis) {
        case RASTER_FLIP_HORIZONTAL: flipCode = 1; break;
        case RASTER_FLIP_VERTICAL:   flipCode = 0; break;
        case RASTER_FLIP_BOTH:       flipCode = -1; break;
        default:
            setLastError("unknown flip axis: " + std::to_string(axis));
            return RASTER_E_INVALID_ARG;
    }
    return raster::internal::guard("Raster_Flip", [&]() -> RasterStatus {
        cv::Mat result;
        cv::flip(src->mat, result, flipCode);
        return raster::internal::emitImage(result, out);
    });
}

RasterStatus RASTER_CALL Raster_Canny(RasterImage src, double low, double high, RasterImage* out)
{
    if (out == nullptr) {
        setLastError("output handle pointer is null");
        return RASTER_E_INVALID_ARG;
    }
    *out = nullptr;
    if (!raster::internal::validImage(src)) {
        return RASTER_E_INVALID_ARG;
    }
    if (low < 0.0 || high < 0.0 || low > high) {
        setLastError("canny thresholds must satisfy 0 <= low <= high");
        return RASTER_E_INVALID_ARG;
    }
    return raster::internal::guard("Raster_Canny", [&]() -> RasterStatus {
        const cv::Mat gray = raster::internal::asGray8(src->mat);
        cv::Mat edges;
        cv::Canny(gray, edges, low, high);
        return raster::internal::emitImage(edges, out);
    });
}

RasterStatus RASTER_CALL Raster_Deskew(RasterImage src, RasterImage* out)
{
    if (out == nullptr) {
        setLastError("output handle pointer is null");
        return RASTER_E_INVALID_ARG;
    }
    *out = nullptr;
    if (!raster::internal::validImage(src)) {
        return RASTER_E_INVALID_ARG;
    }
    return raster::internal::guard("Raster_Deskew", [&]() -> RasterStatus {
        const cv::Mat& mat = src->mat;
        const cv::Mat gray = raster::internal::asGray8(mat);

        // 前景 = 比背景暗的像素（激光雕刻的常见约定：深色 = 要烧掉）。
        cv::Mat binary;
        cv::threshold(gray, binary, 0, 255, cv::THRESH_BINARY_INV | cv::THRESH_OTSU);
        std::vector<cv::Point> pixels;
        cv::findNonZero(binary, pixels);
        if (pixels.size() < 10) {
            // 没有可判定的方向，直接原样返回，不做无意义的旋转。
            return raster::internal::emitImage(mat.clone(), out);
        }

        const cv::RotatedRect box = cv::minAreaRect(pixels);
        double angle = box.angle;
        if (angle < 45.0) {
            angle = -(90.0 - angle);
        }
        if (std::abs(angle) < 0.05) {
            return raster::internal::emitImage(mat.clone(), out);
        }

        const cv::Point2f center(static_cast<float>(mat.cols) * 0.5f,
                                 static_cast<float>(mat.rows) * 0.5f);
        const cv::Mat rotation = cv::getRotationMatrix2D(center, angle, 1.0);
        cv::Mat result;
        cv::warpAffine(mat, result, rotation, mat.size(), cv::INTER_LINEAR, cv::BORDER_CONSTANT,
                       cv::Scalar::all(0));
        return raster::internal::emitImage(result, out);
    });
}

RasterStatus RASTER_CALL Raster_FlatFieldCorrect(RasterImage src, int32_t backgroundRadius,
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
    if (backgroundRadius < 1) {
        setLastError("background radius must be at least 1");
        return RASTER_E_INVALID_ARG;
    }
    return raster::internal::guard("Raster_FlatFieldCorrect", [&]() -> RasterStatus {
        const cv::Mat gray = raster::internal::asGray8(src->mat);

        // 背景估计：降采样 + 大 sigma 高斯，抑制图案、保留照明不均。
        // 降采样是为了让大 sigma 模糊在可接受时间内完成（1MP 图 sigma=64 也不卡）。
        cv::Mat small;
        const double scale = 0.25;
        cv::resize(gray, small, cv::Size(), scale, scale, cv::INTER_AREA);
        const double sigma = std::max(1.0, static_cast<double>(backgroundRadius) * scale);
        cv::GaussianBlur(small, small, cv::Size(), sigma, sigma);
        cv::Mat background;
        cv::resize(small, background, gray.size(), 0, 0, cv::INTER_LINEAR);

        cv::Mat normalized;
        cv::divide(gray, background, normalized, 255.0);
        cv::Mat result;
        normalized.convertTo(result, CV_8U);
        return raster::internal::emitImage(result, out);
    });
}

}  // extern "C"
