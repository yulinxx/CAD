/**
 * @file RasterInspect.cpp
 * @brief 统计与质检：灰度统计、直方图、缺陷检测、两图比对。
 */

#include "Core/RasterInternal.h"

#include <algorithm>
#include <cmath>

namespace
{
    using raster::internal::setLastError;

    /** @brief 追加一个缺陷记录。 */
    void appendDefect(std::vector<RasterDefect>& out, int32_t kind, const cv::Rect& bounds, double area,
                      double severity)
    {
        RasterDefect defect{};
        defect.structSize = sizeof(RasterDefect);
        defect.kind = kind;
        defect.x = bounds.x;
        defect.y = bounds.y;
        defect.width = bounds.width;
        defect.height = bounds.height;
        defect.area = area;
        defect.severity = severity;
        out.push_back(defect);
    }
}  // namespace

extern "C" {

RasterStatus RASTER_CALL Raster_ComputeStats(RasterImage src, RasterStats* out)
{
    if (!raster::internal::checkStructSize(out)) {
        return RASTER_E_STRUCT_SIZE;
    }
    if (!raster::internal::validImage(src)) {
        return RASTER_E_INVALID_ARG;
    }
    const cv::Mat gray = raster::internal::asGray8(src->mat);
    cv::Scalar mean;
    cv::Scalar stdDev;
    cv::meanStdDev(gray, mean, stdDev);
    double minValue = 0.0;
    double maxValue = 0.0;
    cv::minMaxLoc(gray, &minValue, &maxValue);

    out->width = gray.cols;
    out->height = gray.rows;
    out->min = minValue;
    out->max = maxValue;
    out->mean = mean[0];
    out->stdDev = stdDev[0];

    // 墨量占比：灰度 < 128 的像素比例。空白料与过曝扫描都会体现在这个数上。
    const int total = static_cast<int>(gray.total());
    const int dark = total - cv::countNonZero(gray >= 128);
    out->inkRatio = (total > 0) ? static_cast<double>(dark) / static_cast<double>(total) : 0.0;
    return RASTER_OK;
}

RasterStatus RASTER_CALL Raster_Histogram(RasterImage src, RasterHistogram* out)
{
    if (!raster::internal::checkStructSize(out)) {
        return RASTER_E_STRUCT_SIZE;
    }
    if (!raster::internal::validImage(src)) {
        return RASTER_E_INVALID_ARG;
    }
    if (out->bins == nullptr || out->binCount <= 0) {
        setLastError("histogram requires a non-empty caller buffer");
        return RASTER_E_INVALID_ARG;
    }
    const int capacity = out->binCount;
    const cv::Mat gray = raster::internal::asGray8(src->mat);
    // 直方图统计手写而不是用 cv::calcHist：
    //   - calcHist 的 ranges 参数是 const float**，各版本重载签名还在变，
    //     手写一行循环没有这个维护负担；
    //   - 8 位灰度图统计就是一次线性扫描，比 calcHist 的通用路径更快。
    int histogram[256] = {0};
    for (int y = 0; y < gray.rows; ++y) {
        const uint8_t* row = gray.ptr<uint8_t>(y);
        for (int x = 0; x < gray.cols; ++x) {
            ++histogram[row[x]];
        }
    }

    const int bins = std::min(capacity, 256);
    for (int i = 0; i < bins; ++i) {
        out->bins[i] = static_cast<uint32_t>(histogram[i]);
    }
    for (int i = bins; i < capacity; ++i) {
        out->bins[i] = 0;
    }
    out->binCount = bins;
    return RASTER_OK;
}

RasterStatus RASTER_CALL Raster_DetectDefects(RasterImage src, double sensitivity, double minArea,
                                               RasterDefectSet* out)
{
    if (out == nullptr) {
        setLastError("output handle pointer is null");
        return RASTER_E_INVALID_ARG;
    }
    *out = nullptr;
    if (!raster::internal::validImage(src)) {
        return RASTER_E_INVALID_ARG;
    }
    if (sensitivity < 0.0 || sensitivity > 1.0) {
        setLastError("sensitivity must be within [0, 1]");
        return RASTER_E_INVALID_ARG;
    }
    if (minArea < 0.0) {
        setLastError("minArea must not be negative");
        return RASTER_E_INVALID_ARG;
    }

    return raster::internal::guard("Raster_DetectDefects", [&]() -> RasterStatus {
        const cv::Mat gray = raster::internal::asGray8(src->mat);
        const double totalPixels = static_cast<double>(gray.total());
        const double minAreaPixels = std::max(1.0, minArea);

        // 3x3 top-hat 分离背景上的亮点（污点），bottom-hat 分离暗痕（划痕）。
        // 这两类正是工件在雕刻前最常见的瑕疵。
        const cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(3, 3));
        cv::Mat topHat;
        cv::Mat bottomHat;
        cv::morphologyEx(gray, topHat, cv::MORPH_TOPHAT, kernel);
        cv::morphologyEx(gray, bottomHat, cv::MORPH_BLACKHAT, kernel);

        // 灵敏度映射为残差幅度阈值。
        const double spotThreshold = 255.0 * (0.05 + 0.35 * (1.0 - sensitivity));
        const double scratchThreshold = 255.0 * (0.03 + 0.25 * (1.0 - sensitivity));

        std::vector<RasterDefect> defects;

        auto collect = [&](const cv::Mat& residual, double threshold, int32_t kind, double areaScale) {
            cv::Mat mask;
            cv::threshold(residual, mask, threshold, 255, cv::THRESH_BINARY);
            cv::Mat labels;
            cv::Mat stats;
            cv::Mat centroids;
            const int componentCount =
                cv::connectedComponentsWithStats(mask, labels, stats, centroids, 8, CV_32S);
            for (int label = 1; label < componentCount; ++label) {
                const double area = static_cast<double>(stats.at<int>(label, cv::CC_STAT_AREA));
                if (area < minAreaPixels) {
                    continue;
                }
                const cv::Rect bounds(stats.at<int>(label, cv::CC_STAT_LEFT),
                                      stats.at<int>(label, cv::CC_STAT_TOP),
                                      stats.at<int>(label, cv::CC_STAT_WIDTH),
                                      stats.at<int>(label, cv::CC_STAT_HEIGHT));
                const double coverage = (totalPixels > 0.0) ? area / totalPixels : 0.0;
                const double severity = std::min(1.0, coverage * areaScale);
                appendDefect(defects, kind, bounds, area, severity);
            }
        };

        collect(topHat, spotThreshold, RASTER_DEFECT_SPOT, 400.0);
        collect(bottomHat, scratchThreshold, RASTER_DEFECT_SCRATCH, 200.0);

        // 孔洞：明显亮于局部背景的区域（反相后同样用 top-hat 提取）。
        cv::Mat inverted;
        cv::bitwise_not(gray, inverted);
        cv::Mat holeResidual;
        cv::morphologyEx(inverted, holeResidual, cv::MORPH_TOPHAT, kernel);
        collect(holeResidual, spotThreshold * 0.8, RASTER_DEFECT_HOLE, 300.0);

        std::sort(defects.begin(), defects.end(),
                  [](const RasterDefect& a, const RasterDefect& b) { return a.severity > b.severity; });

        auto* handle = new RasterDefectHandle();
        handle->defects = std::move(defects);
        *out = handle;
        return RASTER_OK;
    });
}

RasterStatus RASTER_CALL Raster_DefectSetCount(RasterDefectSet set, int32_t* out)
{
    if (out == nullptr) {
        setLastError("output pointer is null");
        return RASTER_E_INVALID_ARG;
    }
    if (set == nullptr) {
        setLastError("defect set handle is null");
        return RASTER_E_INVALID_ARG;
    }
    *out = static_cast<int32_t>(set->defects.size());
    return RASTER_OK;
}

RasterStatus RASTER_CALL Raster_DefectSetGet(RasterDefectSet set, int32_t index, RasterDefect* out)
{
    if (!raster::internal::checkStructSize(out)) {
        return RASTER_E_STRUCT_SIZE;
    }
    if (set == nullptr) {
        setLastError("defect set handle is null");
        return RASTER_E_INVALID_ARG;
    }
    if (index < 0 || static_cast<std::size_t>(index) >= set->defects.size()) {
        setLastError("defect index out of range: " + std::to_string(index));
        return RASTER_E_NOT_FOUND;
    }
    *out = set->defects[static_cast<std::size_t>(index)];
    return RASTER_OK;
}

void RASTER_CALL Raster_DestroyDefectSet(RasterDefectSet set)
{
    delete set;
}

RasterStatus RASTER_CALL Raster_CompareImages(RasterImage a, RasterImage b,
                                                RasterCompareResult* out)
{
    if (!raster::internal::checkStructSize(out)) {
        return RASTER_E_STRUCT_SIZE;
    }
    if (!raster::internal::validImage(a) || !raster::internal::validImage(b)) {
        return RASTER_E_INVALID_ARG;
    }
    const cv::Mat lhs = raster::internal::asGray8(a->mat);
    const cv::Mat rhs = raster::internal::asGray8(b->mat);
    if (lhs.size() != rhs.size()) {
        setLastError("image sizes differ: " + std::to_string(lhs.cols) + "x" +
                     std::to_string(lhs.rows) + " vs " + std::to_string(rhs.cols) + "x" +
                     std::to_string(rhs.rows));
        return RASTER_E_INVALID_ARG;
    }

    cv::Mat diff;
    cv::absdiff(lhs, rhs, diff);
    cv::Scalar mean;
    cv::Scalar stdDev;
    cv::meanStdDev(diff, mean, stdDev);
    double minValue = 0.0;
    double maxValue = 0.0;
    cv::minMaxLoc(diff, &minValue, &maxValue);

    const double totalPixels = static_cast<double>(diff.total());
    const int differing = cv::countNonZero(diff);
    double sumSquares = 0.0;
    for (int y = 0; y < diff.rows; ++y) {
        const uint8_t* row = diff.ptr<uint8_t>(y);
        for (int x = 0; x < diff.cols; ++x) {
            const double value = static_cast<double>(row[x]);
            sumSquares += value * value;
        }
    }
    const double rmse = (totalPixels > 0.0) ? std::sqrt(sumSquares / totalPixels) : 0.0;

    out->differingPixels = differing;
    out->totalPixels = static_cast<int32_t>(totalPixels);
    out->meanAbsDiff = mean[0];
    out->maxAbsDiff = maxValue;
    out->rmse = rmse;
    out->similarity = std::max(0.0, 1.0 - rmse / 255.0);
    return RASTER_OK;
}

}  // extern "C"
