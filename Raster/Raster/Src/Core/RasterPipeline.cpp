/**
 * @file RasterPipeline.cpp
 * @brief 处理管线：把一串位图算子固化成可复用的工艺流程。
 *
 * 管线存在的意义不是"少写几行代码"，而是**工艺可复现**：
 * 一套雕刻前处理（去噪 → 平场 → 二值化 → 去孤点 → 轮廓）要同时跑在
 * 调试机、生产机、客户自研的软件里，就必须有一份**唯一**的定义。
 * 管线就是这份定义：可存、可传、可在多台设备上跑出同样的结果。
 */

#include "Core/RasterInternal.h"

#include <algorithm>

namespace
{
    using raster::internal::setLastError;

    /** @brief 去掉图像四周 i0 像素的边框残留。 */
    RasterStatus erodeBorder(const cv::Mat& src, int borderPixels, cv::Mat& out)
    {
        const int border = std::max(0, borderPixels);
        if (border * 2 >= std::min(src.cols, src.rows)) {
            setLastError("border size is too large for the image");
            return RASTER_E_INVALID_ARG;
        }
        const cv::Rect inner(border, border, src.cols - border * 2, src.rows - border * 2);
        out = cv::Mat::zeros(src.size(), src.type());
        src(inner).copyTo(out(inner));
        return RASTER_OK;
    }

    /** @brief 执行单个算子。 */
    RasterStatus applyOp(int32_t opId, const RasterOpParams& params, RasterImage input,
                         RasterImage& output)
    {
        switch (opId) {
            case RASTER_OP_TO_GRAY:
                return Raster_ToGray(input, &output);
            case RASTER_OP_INVERT:
                return Raster_Invert(input, &output);
            case RASTER_OP_ADJUST_CONTRAST:
                return Raster_AdjustBrightnessContrast(input, params.f0, params.f1, &output);
            case RASTER_OP_THRESHOLD:
                return Raster_Threshold(input, params.i0, params.f0, params.f1, &output);
            case RASTER_OP_OTSU:
                return Raster_Threshold(input, RASTER_THRESH_OTSU, 0.0, params.f0, &output);
            case RASTER_OP_CLAHE:
                return Raster_Clahe(input, params.f0 > 0.0 ? params.f0 : 2.0,
                                    params.i0 > 0 ? params.i0 : 8, &output);
            case RASTER_OP_EQUALIZE:
                return Raster_EqualizeHist(input, &output);
            case RASTER_OP_DENOISE:
                return Raster_Denoise(input, params.i0, params.f0, &output);
            case RASTER_OP_SHARPEN:
                return Raster_Sharpen(input, params.f0, &output);
            case RASTER_OP_MORPHOLOGY:
                return Raster_Morphology(input, params.i0, params.i1,
                                         params.i2 > 0 ? params.i2 : 3, &output);
            case RASTER_OP_RESIZE:
                return Raster_Resize(input, params.f0, params.f1, params.i0, &output);
            case RASTER_OP_ROTATE:
                return Raster_Rotate(input, params.f0, params.i0, params.f1, &output);
            case RASTER_OP_CROP:
                return Raster_Crop(input, params.i0, params.i1, params.i2, params.i3, &output);
            case RASTER_OP_FLIP:
                return Raster_Flip(input, params.i0, &output);
            case RASTER_OP_DITHER:
                return Raster_Dither(input, params.i0, &output);
            case RASTER_OP_HALFTONE:
                return Raster_Halftone(input, params.i0 > 1 ? params.i0 : 6, params.f0, params.i1,
                                        &output);
            case RASTER_OP_DESPECKLE:
                return Raster_Despeckle(input, params.i0, params.i1, &output);
            case RASTER_OP_CANNY:
                return Raster_Canny(input, params.f0, params.f1, &output);
            case RASTER_OP_DESKEW:
                return Raster_Deskew(input, &output);
            case RASTER_OP_HEIGHT_MAP:
                return Raster_HeightMapFromGray(input, params.f0, params.f1, &output);
            case RASTER_OP_FLAT_FIELD:
                return Raster_FlatFieldCorrect(input, params.i0 > 0 ? params.i0 : 31, &output);
            case RASTER_OP_ERODE_BORDER: {
                return raster::internal::guard("RASTER_OP_ERODE_BORDER", [&]() -> RasterStatus {
                    cv::Mat result;
                    const RasterStatus status = erodeBorder(input->mat, params.i0, result);
                    if (status != RASTER_OK) {
                        return status;
                    }
                    output = raster::internal::makeImage(result);
                    return RASTER_OK;
                });
            }
            default:
                setLastError("unknown pipeline op id: " + std::to_string(opId));
                return RASTER_E_INVALID_ARG;
        }
    }
}  // namespace

extern "C" {

RasterStatus RASTER_CALL Raster_CreatePipeline(RasterPipeline* out)
{
    if (out == nullptr) {
        setLastError("output handle pointer is null");
        return RASTER_E_INVALID_ARG;
    }
    *out = nullptr;
    auto* handle = new RasterPipelineHandle();
    *out = handle;
    return RASTER_OK;
}

RasterStatus RASTER_CALL Raster_PipelineAdd(RasterPipeline pipeline, int32_t opId,
                                            const void* params, size_t paramsSize)
{
    if (pipeline == nullptr) {
        setLastError("pipeline handle is null");
        return RASTER_E_INVALID_ARG;
    }
    if (opId <= RASTER_OP_NONE || opId > RASTER_OP_ERODE_BORDER) {
        setLastError("unknown pipeline op id: " + std::to_string(opId));
        return RASTER_E_INVALID_ARG;
    }
    RasterOpParams effective{};
    effective.structSize = sizeof(RasterOpParams);
    effective.opId = opId;
    if (params != nullptr) {
        if (paramsSize != sizeof(RasterOpParams)) {
            setLastError("paramsSize must equal sizeof(RasterOpParams)");
            return RASTER_E_STRUCT_SIZE;
        }
        std::memcpy(&effective, params, sizeof(RasterOpParams));
        effective.structSize = sizeof(RasterOpParams);
        effective.opId = opId;
    }
    pipeline->ops.push_back(raster::internal::OpEntry{opId, effective});
    return RASTER_OK;
}

RasterStatus RASTER_CALL Raster_PipelineClear(RasterPipeline pipeline)
{
    if (pipeline == nullptr) {
        setLastError("pipeline handle is null");
        return RASTER_E_INVALID_ARG;
    }
    pipeline->ops.clear();
    return RASTER_OK;
}

RasterStatus RASTER_CALL Raster_PipelineCount(RasterPipeline pipeline, int32_t* out)
{
    if (out == nullptr) {
        setLastError("output pointer is null");
        return RASTER_E_INVALID_ARG;
    }
    if (pipeline == nullptr) {
        setLastError("pipeline handle is null");
        return RASTER_E_INVALID_ARG;
    }
    *out = static_cast<int32_t>(pipeline->ops.size());
    return RASTER_OK;
}

RasterStatus RASTER_CALL Raster_PipelineExecute(RasterPipeline pipeline, RasterImage src,
                                                RasterImage* out)
{
    if (out == nullptr) {
        setLastError("output handle pointer is null");
        return RASTER_E_INVALID_ARG;
    }
    *out = nullptr;
    if (pipeline == nullptr) {
        setLastError("pipeline handle is null");
        return RASTER_E_INVALID_ARG;
    }
    if (!raster::internal::validImage(src)) {
        return RASTER_E_INVALID_ARG;
    }
    if (pipeline->ops.empty()) {
        setLastError("pipeline has no operators");
        return RASTER_E_EMPTY;
    }

    RasterImage current = nullptr;
    RasterStatus status = RASTER_OK;
    for (const raster::internal::OpEntry& entry : pipeline->ops) {
        RasterImage next = nullptr;
        status = applyOp(entry.opId, entry.params, current ? current : src, next);
        if (status != RASTER_OK) {
            if (current) {
                Raster_DestroyImage(current);
            }
            setLastError("pipeline op " + std::to_string(entry.opId) + " failed: " +
                         Raster_StatusText(static_cast<int32_t>(status)));
            return status;
        }
        if (current) {
            Raster_DestroyImage(current);
        }
        current = next;
    }
    *out = current;
    return RASTER_OK;
}

void RASTER_CALL Raster_DestroyPipeline(RasterPipeline pipeline)
{
    delete pipeline;
}

}  // extern "C"
