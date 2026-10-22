/**
 * @file RasterContour.cpp
 * @brief Contour extraction, kerf compensation and toolpath ordering.
 */

#include "Core/RasterInternal.h"

#include <algorithm>
#include <cmath>

namespace
{
    using raster::internal::ContourData;
    using raster::internal::setLastError;

    /** Build the module's contour collection from OpenCV contours plus hierarchy. */
    void buildContourSet(const std::vector<std::vector<cv::Point> >& contours,
                         const std::vector<cv::Vec4i>& hierarchy, double minArea, double epsilon,
                         std::vector<ContourData>& out)
    {
        for (std::size_t i = 0; i < contours.size(); ++i) {
            const double area = std::abs(cv::contourArea(contours[i]));
            if (area < minArea) {
                continue;
            }
            std::vector<cv::Point> points;
            if (epsilon > 0.0) {
                cv::approxPolyDP(contours[i], points, epsilon, true);
            } else {
                points = contours[i];
            }
            if (points.size() < 3) {
                continue;
            }

            ContourData data;
            data.points.reserve(points.size() * 2);
            for (const cv::Point& point : points) {
                data.points.push_back(static_cast<double>(point.x));
                data.points.push_back(static_cast<double>(point.y));
            }
            data.area = area;
            data.perimeter = cv::arcLength(contours[i], true);
            // Under RETR_CCOMP a contour with a parent is a hole.
            data.isHole = (i < hierarchy.size()) && (hierarchy[i][3] != -1);
            data.bounds = cv::boundingRect(points);
            out.push_back(std::move(data));
        }
    }

    /** Extract contours (with hole hierarchy) from a binary mask. */
    void extractFromMask(const cv::Mat& binary, double minArea, double epsilon,
                         std::vector<ContourData>& out)
    {
        std::vector<std::vector<cv::Point> > contours;
        std::vector<cv::Vec4i> hierarchy;
        cv::findContours(binary, contours, hierarchy, cv::RETR_CCOMP, cv::CHAIN_APPROX_SIMPLE);
        buildContourSet(contours, hierarchy, minArea, epsilon, out);
    }

    /** Union bounding box of every contour in the set. */
    cv::Rect2f totalBounds(const std::vector<ContourData>& contours)
    {
        cv::Rect2f bounds;
        bool first = true;
        for (const ContourData& contour : contours) {
            if (contour.points.size() < 6) {
                continue;
            }
            const cv::Rect2f one(static_cast<float>(contour.bounds.x),
                                 static_cast<float>(contour.bounds.y),
                                 static_cast<float>(contour.bounds.width),
                                 static_cast<float>(contour.bounds.height));
            bounds = first ? one : bounds | one;
            first = false;
        }
        return bounds;
    }
}  // namespace

extern "C" {

RasterStatus RASTER_CALL Raster_TraceContours(RasterImage src, int32_t mode, double minArea,
                                              double approxEpsilon, RasterContourSet* out)
{
    if (out == nullptr) {
        setLastError("output handle pointer is null");
        return RASTER_E_INVALID_ARG;
    }
    *out = nullptr;
    if (!raster::internal::validImage(src)) {
        return RASTER_E_INVALID_ARG;
    }
    if (minArea < 0.0) {
        setLastError("minArea must not be negative");
        return RASTER_E_INVALID_ARG;
    }
    return raster::internal::guard("Raster_TraceContours", [&]() -> RasterStatus {
        // clone() 断开与源图的数据共享：findContours / threshold 都可能就地改写。
        cv::Mat binary = raster::internal::asGray8(src->mat).clone();
        // Binarize first when the input is not already binary, so that
        // "bright = keep" is the default semantic.
        const int nonZero = cv::countNonZero(binary);
        if (nonZero != 0 && nonZero != static_cast<int>(binary.total())) {
            cv::threshold(binary, binary, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
        }

        std::vector<ContourData> all;
        extractFromMask(binary, minArea, approxEpsilon, all);

        std::vector<ContourData> filtered;
        filtered.reserve(all.size());
        for (const ContourData& contour : all) {
            const bool keep = (mode == RASTER_TRACE_ALL) ||
                              (mode == RASTER_TRACE_EXTERNAL && !contour.isHole) ||
                              (mode == RASTER_TRACE_HOLES && contour.isHole);
            if (keep) {
                filtered.push_back(contour);
            }
        }

        auto* handle = new RasterContourHandle();
        handle->contours = std::move(filtered);
        *out = handle;
        return RASTER_OK;
    });
}

RasterStatus RASTER_CALL Raster_KerfCompensate(RasterContourSet src, double kerfWidth, int32_t side,
                                               RasterContourSet* out)
{
    if (out == nullptr) {
        setLastError("output handle pointer is null");
        return RASTER_E_INVALID_ARG;
    }
    *out = nullptr;
    if (!raster::internal::validContourSet(src)) {
        return RASTER_E_INVALID_ARG;
    }
    if (kerfWidth < 0.0) {
        setLastError("kerf width must not be negative");
        return RASTER_E_INVALID_ARG;
    }
    if (src->contours.empty()) {
        setLastError("contour set is empty");
        return RASTER_E_EMPTY;
    }
    if (side != RASTER_KERF_OUTSIDE && side != RASTER_KERF_INSIDE && side != RASTER_KERF_CENTER) {
        setLastError("unknown kerf side: " + std::to_string(side));
        return RASTER_E_INVALID_ARG;
    }

    return raster::internal::guard("Raster_KerfCompensate", [&]() -> RasterStatus {
        if (side == RASTER_KERF_CENTER || kerfWidth <= 0.0) {
            auto* handle = new RasterContourHandle();
            handle->contours = src->contours;
            *out = handle;
            return RASTER_OK;
        }

        // Offset implementation: rasterize the polygons into a mask, apply a
        // morphological dilate/erode, then re-extract the contours. This works
        // for arbitrary shapes; a vertex based offset breaks on sharp corners.
        const cv::Rect2f bounds = totalBounds(src->contours);
        if (bounds.width <= 0.0f || bounds.height <= 0.0f) {
            setLastError("contour set has a degenerate bounding box");
            return RASTER_E_INVALID_ARG;
        }
        const int radius = std::max(1, static_cast<int>(std::lround(kerfWidth * 0.5)));
        const int padding = radius + 2;
        const int offsetX = padding - static_cast<int>(std::floor(bounds.x));
        const int offsetY = padding - static_cast<int>(std::floor(bounds.y));
        const int width = static_cast<int>(std::ceil(bounds.width)) + padding * 2;
        const int height = static_cast<int>(std::ceil(bounds.height)) + padding * 2;

        cv::Mat mask(height, width, CV_8UC1, cv::Scalar::all(0));
        std::vector<std::vector<cv::Point> > polygons;
        polygons.reserve(src->contours.size());
        for (const ContourData& contour : src->contours) {
            std::vector<cv::Point> polygon;
            polygon.reserve(contour.points.size() / 2);
            for (std::size_t i = 0; i + 1 < contour.points.size(); i += 2) {
                polygon.emplace_back(static_cast<int>(std::lround(contour.points[i])) + offsetX,
                                     static_cast<int>(std::lround(contour.points[i + 1])) + offsetY);
            }
            if (polygon.size() >= 3) {
                polygons.push_back(std::move(polygon));
            }
        }
        if (polygons.empty()) {
            setLastError("contour set contains no usable polygon");
            return RASTER_E_EMPTY;
        }
        cv::fillPoly(mask, polygons, cv::Scalar(255));

        const cv::Mat kernel = cv::getStructuringElement(
            cv::MORPH_ELLIPSE, cv::Size(radius * 2 + 1, radius * 2 + 1));
        cv::Mat offsetMask;
        if (side == RASTER_KERF_OUTSIDE) {
            cv::dilate(mask, offsetMask, kernel);
        } else {
            cv::erode(mask, offsetMask, kernel);
        }

        std::vector<ContourData> compensated;
        extractFromMask(offsetMask, 0.0, 0.0, compensated);

        // Move the result back into the original coordinate system.
        for (ContourData& contour : compensated) {
            for (std::size_t i = 0; i + 1 < contour.points.size(); i += 2) {
                contour.points[i] -= static_cast<double>(offsetX);
                contour.points[i + 1] -= static_cast<double>(offsetY);
            }
            contour.bounds.x -= offsetX;
            contour.bounds.y -= offsetY;
        }

        auto* handle = new RasterContourHandle();
        handle->contours = std::move(compensated);
        *out = handle;
        return RASTER_OK;
    });
}

RasterStatus RASTER_CALL Raster_OrderContours(RasterContourSet src, int32_t mode,
                                               RasterContourSet* out)
{
    if (out == nullptr) {
        setLastError("output handle pointer is null");
        return RASTER_E_INVALID_ARG;
    }
    *out = nullptr;
    if (!raster::internal::validContourSet(src)) {
        return RASTER_E_INVALID_ARG;
    }
    if (src->contours.empty()) {
        setLastError("contour set is empty");
        return RASTER_E_EMPTY;
    }

    std::vector<ContourData> ordered = src->contours;
    switch (mode) {
        case RASTER_ORDER_INNER_FIRST:
            std::stable_sort(ordered.begin(), ordered.end(),
                             [](const ContourData& a, const ContourData& b) {
                                 if (a.isHole != b.isHole) {
                                     return static_cast<int>(a.isHole) > static_cast<int>(b.isHole);
                                 }
                                 return a.area < b.area;
                             });
            break;
        case RASTER_ORDER_OUTER_FIRST:
            std::stable_sort(ordered.begin(), ordered.end(),
                             [](const ContourData& a, const ContourData& b) {
                                 if (a.isHole != b.isHole) {
                                     return static_cast<int>(a.isHole) < static_cast<int>(b.isHole);
                                 }
                                 return a.area > b.area;
                             });
            break;
        case RASTER_ORDER_BY_AREA_DESC:
            std::stable_sort(ordered.begin(), ordered.end(),
                             [](const ContourData& a, const ContourData& b) { return a.area > b.area; });
            break;
        case RASTER_ORDER_BY_AREA_ASC:
            std::stable_sort(ordered.begin(), ordered.end(),
                             [](const ContourData& a, const ContourData& b) { return a.area < b.area; });
            break;
        case RASTER_ORDER_BY_LENGTH_DESC:
            std::stable_sort(ordered.begin(), ordered.end(),
                             [](const ContourData& a, const ContourData& b) {
                                 return a.perimeter > b.perimeter;
                             });
            break;
        case RASTER_ORDER_INPUT:
        default:
            break;
    }

    auto* handle = new RasterContourHandle();
    handle->contours = std::move(ordered);
    *out = handle;
    return RASTER_OK;
}

RasterStatus RASTER_CALL Raster_ContourSetCount(RasterContourSet set, int32_t* out)
{
    if (out == nullptr) {
        setLastError("output pointer is null");
        return RASTER_E_INVALID_ARG;
    }
    if (!raster::internal::validContourSet(set)) {
        return RASTER_E_INVALID_ARG;
    }
    *out = static_cast<int32_t>(set->contours.size());
    return RASTER_OK;
}

RasterStatus RASTER_CALL Raster_ContourSetGet(RasterContourSet set, int32_t index,
                                               RasterContour* out)
{
    if (!raster::internal::checkStructSize(out)) {
        return RASTER_E_STRUCT_SIZE;
    }
    if (!raster::internal::validContourSet(set)) {
        return RASTER_E_INVALID_ARG;
    }
    if (index < 0 || static_cast<std::size_t>(index) >= set->contours.size()) {
        setLastError("contour index out of range: " + std::to_string(index));
        return RASTER_E_NOT_FOUND;
    }
    const ContourData& contour = set->contours[static_cast<std::size_t>(index)];
    out->pointCount = static_cast<int32_t>(contour.points.size() / 2);
    out->isHole = contour.isHole ? 1 : 0;
    out->reserved = 0;
    out->points = contour.points.empty() ? nullptr : contour.points.data();
    return RASTER_OK;
}

RasterStatus RASTER_CALL Raster_ContourSetGetInfo(RasterContourSet set, int32_t index,
                                                    RasterContourInfo* out)
{
    if (!raster::internal::checkStructSize(out)) {
        return RASTER_E_STRUCT_SIZE;
    }
    if (!raster::internal::validContourSet(set)) {
        return RASTER_E_INVALID_ARG;
    }
    if (index < 0 || static_cast<std::size_t>(index) >= set->contours.size()) {
        setLastError("contour index out of range: " + std::to_string(index));
        return RASTER_E_NOT_FOUND;
    }
    const ContourData& contour = set->contours[static_cast<std::size_t>(index)];
    out->pointCount = static_cast<int32_t>(contour.points.size() / 2);
    out->isHole = contour.isHole ? 1 : 0;
    out->reserved = 0;
    out->area = contour.area;
    out->perimeter = contour.perimeter;
    out->minX = static_cast<double>(contour.bounds.x);
    out->minY = static_cast<double>(contour.bounds.y);
    out->maxX = static_cast<double>(contour.bounds.x + contour.bounds.width);
    out->maxY = static_cast<double>(contour.bounds.y + contour.bounds.height);
    return RASTER_OK;
}

void RASTER_CALL Raster_DestroyContourSet(RasterContourSet set)
{
    delete set;
}

}  // extern "C"
