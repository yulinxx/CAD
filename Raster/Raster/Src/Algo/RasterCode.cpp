/**
 * @file RasterCode.cpp
 * @brief 码识别：二维码与一维码的检测 / 解码（激光打标防错与追溯）。
 *
 * 为什么放在位图处理模块而不是 Vision：
 *   打标后的复核是**离线**动作——工件已经离开设备，拿一张照片（或设备自己拍的
 *   一张）来校验料号。它不需要标定、不需要相机、不需要在线流水线，
 *   只需要一张图和一个期望值。放在 Raster 里可以让"打标 -> 复核"整条链路
 *   在同一个 DLL 里闭环。
 */

#include "Core/RasterInternal.h"

#include <opencv2/objdetect.hpp>
#include <opencv2/objdetect/barcode.hpp>

#include <algorithm>

namespace
{
    using raster::internal::setLastError;

    /** @brief 由若干点求出轴对齐包围盒。 */
    void boundsOf(const std::vector<cv::Point2f>& points, int32_t& x, int32_t& y, int32_t& width,
                  int32_t& height)
    {
        if (points.empty()) {
            x = 0;
            y = 0;
            width = 0;
            height = 0;
            return;
        }
        float minX = points[0].x;
        float maxX = points[0].x;
        float minY = points[0].y;
        float maxY = points[0].y;
        for (const cv::Point2f& point : points) {
            minX = std::min(minX, point.x);
            maxX = std::max(maxX, point.x);
            minY = std::min(minY, point.y);
            maxY = std::max(maxY, point.y);
        }
        x = static_cast<int32_t>(std::floor(minX));
        y = static_cast<int32_t>(std::floor(minY));
        width = static_cast<int32_t>(std::ceil(maxX) - std::floor(minX));
        height = static_cast<int32_t>(std::ceil(maxY) - std::floor(minY));
    }
}  // namespace

extern "C" {

RasterStatus RASTER_CALL Raster_DetectCodes(RasterImage src, RasterCodeSet* out)
{
    if (out == nullptr) {
        setLastError("output handle pointer is null");
        return RASTER_E_INVALID_ARG;
    }
    *out = nullptr;
    if (!raster::internal::validImage(src)) {
        return RASTER_E_INVALID_ARG;
    }

    return raster::internal::guard("Raster_DetectCodes", [&]() -> RasterStatus {
        const cv::Mat gray = raster::internal::asGray8(src->mat);
        std::vector<raster::internal::CodeData> codes;

        // --- 二维码 ---
        // detectAndDecode 返回解码文本（空串 = 没解出来），点位是四角。
        cv::QRCodeDetector qrDetector;
        std::vector<cv::Point2f> qrPoints;
        const std::string qrText = qrDetector.detectAndDecode(gray, qrPoints);
        if (!qrPoints.empty() || !qrText.empty()) {
            raster::internal::CodeData data;
            data.kind = qrText.empty() ? RASTER_CODE_UNKNOWN : RASTER_CODE_QR;
            data.text = qrText;
            data.confidence = qrText.empty() ? 0.0 : 1.0;
            boundsOf(qrPoints, data.x, data.y, data.width, data.height);
            codes.push_back(std::move(data));
        }

        // --- 一维码 ---
        // 一维码后端（zbar/ZXing）在部分 OpenCV 构建里不存在，
        // 构造 BarcodeDetector 会直接抛异常——此时降级为"只支持二维码"，
        // 而不是让整个调用失败。
        try {
            cv::barcode::BarcodeDetector barcodeDetector;
            std::vector<std::string> decoded;
            std::vector<std::string> types;
            std::vector<cv::Point2f> points;
            if (barcodeDetector.detectAndDecodeWithType(gray, decoded, types, points)) {
                const std::size_t count = std::min(decoded.size(), types.size());
                for (std::size_t i = 0; i < count; ++i) {
                    raster::internal::CodeData data;
                    data.kind = decoded[i].empty() ? RASTER_CODE_UNKNOWN : RASTER_CODE_BARCODE;
                    data.text = decoded[i];
                    data.confidence = decoded[i].empty() ? 0.0 : 1.0;
                    // 点位是 [N][4] 的扁平数组。
                    const std::size_t begin = i * 4;
                    if (begin + 4 <= points.size()) {
                        const std::vector<cv::Point2f> quad(points.begin() + static_cast<long>(begin),
                                                            points.begin() + static_cast<long>(begin + 4));
                        boundsOf(quad, data.x, data.y, data.width, data.height);
                    }
                    codes.push_back(std::move(data));
                }
            }
        } catch (const cv::Exception& ex) {
            setLastError(std::string("barcode backend unavailable, QR only: ") + ex.what());
        }

        auto* handle = new RasterCodeHandle();
        handle->codes = std::move(codes);
        *out = handle;
        return RASTER_OK;
    });
}

RasterStatus RASTER_CALL Raster_VerifyCode(RasterImage src, const char* expected, int32_t* out)
{
    if (out == nullptr) {
        setLastError("output pointer is null");
        return RASTER_E_INVALID_ARG;
    }
    *out = 0;
    if (expected == nullptr || *expected == '\0') {
        setLastError("expected code text is null or empty");
        return RASTER_E_INVALID_ARG;
    }
    if (!raster::internal::validImage(src)) {
        return RASTER_E_INVALID_ARG;
    }

    RasterCodeSet set = nullptr;
    const RasterStatus status = Raster_DetectCodes(src, &set);
    if (status != RASTER_OK) {
        return status;
    }
    const std::string wanted(expected);

    int32_t count = 0;
    Raster_CodeSetCount(set, &count);
    bool matched = false;
    bool anyDecoded = false;
    for (int32_t i = 0; i < count; ++i) {
        RasterCode code{};
        code.structSize = sizeof(RasterCode);
        if (Raster_CodeSetGet(set, i, &code) != RASTER_OK) {
            continue;
        }
        if (code.text == nullptr || code.text[0] == '\0') {
            continue;
        }
        anyDecoded = true;
        if (wanted == code.text) {
            matched = true;
            break;
        }
    }
    Raster_DestroyCodeSet(set);

    if (!anyDecoded) {
        setLastError("no decodable code found in the image");
        return RASTER_E_NOT_FOUND;
    }
    *out = matched ? 1 : 0;
    return RASTER_OK;
}

RasterStatus RASTER_CALL Raster_CodeSetCount(RasterCodeSet set, int32_t* out)
{
    if (out == nullptr) {
        setLastError("output pointer is null");
        return RASTER_E_INVALID_ARG;
    }
    if (set == nullptr) {
        setLastError("code set handle is null");
        return RASTER_E_INVALID_ARG;
    }
    *out = static_cast<int32_t>(set->codes.size());
    return RASTER_OK;
}

RasterStatus RASTER_CALL Raster_CodeSetGet(RasterCodeSet set, int32_t index, RasterCode* out)
{
    if (!raster::internal::checkStructSize(out)) {
        return RASTER_E_STRUCT_SIZE;
    }
    if (set == nullptr) {
        setLastError("code set handle is null");
        return RASTER_E_INVALID_ARG;
    }
    if (index < 0 || static_cast<std::size_t>(index) >= set->codes.size()) {
        setLastError("code index out of range: " + std::to_string(index));
        return RASTER_E_NOT_FOUND;
    }
    const raster::internal::CodeData& code = set->codes[static_cast<std::size_t>(index)];
    out->kind = code.kind;
    out->x = code.x;
    out->y = code.y;
    out->width = code.width;
    out->height = code.height;
    out->reserved = 0;
    out->confidence = code.confidence;
    // 借用指针：指向集合内部持有的字符串，集合销毁后失效。
    out->text = code.text.c_str();
    return RASTER_OK;
}

void RASTER_CALL Raster_DestroyCodeSet(RasterCodeSet set)
{
    delete set;
}

}  // extern "C"
