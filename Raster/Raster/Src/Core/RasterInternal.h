#pragma once

/**
 * @file RasterInternal.h
 * @brief Raster 模块内部共享定义（**不安装、不导出**）。
 *
 * 这里的很多东西（cv::Mat、std::string、std::vector）都是**故意**不许出现在
 * 公开头里的：它们只存在于 DLL 内部，跨边界一律退化成 POD + 句柄。
 */

#include <opencv2/opencv.hpp>

#include "Raster/RasterApi.h"

#include <cstdint>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// 句柄的实体定义
// ---------------------------------------------------------------------------
// C 头里只前向声明了这些 struct，实体定义必须在**全局命名空间**，
// 否则 C 头的 typedef 与这里的定义对不上（ODR 违规，且类型不同 → 编译错）。

namespace raster
{
    namespace internal
    {
        /** @brief 一条轮廓的内部表示。 */
        struct ContourData
        {
            std::vector<double> points;  ///< x,y 交替
            bool isHole = false;
            double area = 0.0;
            double perimeter = 0.0;
            cv::Rect bounds{0, 0, 0, 0};
        };

        /** @brief 管线中的一个算子。 */
        struct OpEntry
        {
            int32_t opId = RASTER_OP_NONE;
            RasterOpParams params{};
        };

        /** @brief 一个被识别出来的码。 */
        struct CodeData
        {
            int32_t kind = RASTER_CODE_UNKNOWN;
            int32_t x = 0;
            int32_t y = 0;
            int32_t width = 0;
            int32_t height = 0;
            double confidence = 0.0;
            std::string text;
        };
    }  // namespace internal
}  // namespace raster

/** @brief 图像句柄实体。 */
struct RasterImageHandle
{
    cv::Mat mat;
};

/** @brief 轮廓集合句柄实体。 */
struct RasterContourHandle
{
    std::vector<raster::internal::ContourData> contours;
};

/** @brief 缺陷集合句柄实体。 */
struct RasterDefectHandle
{
    std::vector<RasterDefect> defects;
};

/** @brief 管线句柄实体。 */
struct RasterPipelineHandle
{
    std::vector<raster::internal::OpEntry> ops;
};

/** @brief 码集合句柄实体。 */
struct RasterCodeHandle
{
    std::vector<raster::internal::CodeData> codes;
};

namespace raster
{
    namespace internal
    {
        // -------------------------------------------------------------------
        // 错误信息（线程局部）
        // -------------------------------------------------------------------

        /** @brief 记录最近一次失败的详细原因。 */
        void setLastError(const std::string& message);

        /** @brief 读取最近一次失败的详细原因。 */
        std::string lastError();

        // -------------------------------------------------------------------
        // structSize 校验
        // -------------------------------------------------------------------
        //
        // 约定：所有出入参结构体的首字段是 structSize。库端据此判断调用方
        // 编译时的结构体版本，从而在**尾部追加字段**时保持二进制兼容。
        //
        // 实现方式：为每个结构体维护一个"可接受尺寸集合"。
        //   - 当前版本只有 sizeof(T)；
        //   - 将来追加字段后，把旧尺寸也加进集合，并在读取新字段处按
        //     "该字段偏移 + 尺寸 <= 调用方尺寸" 决定是否读取。
        //
        // 尺寸不匹配一律返回 RASTER_E_STRUCT_SIZE，而不是越界读写 ——
        // 后者会把内存踩坏，故障现场还完全对不上号。

        template <typename T>
        bool checkStructSize(const T* candidate, std::initializer_list<uint32_t> accepted = {sizeof(T)})
        {
            if (candidate == nullptr) {
                setLastError("null struct pointer");
                return false;
            }
            uint32_t given = 0;
            std::memcpy(&given, candidate, sizeof(given));
            for (uint32_t size : accepted) {
                if (given == size) {
                    return true;
                }
            }
            setLastError("structSize mismatch: expected " + std::to_string(sizeof(T)) +
                         ", got " + std::to_string(given) +
                         " (ABI mismatch between caller and library)");
            return false;
        }

        // -------------------------------------------------------------------
        // 像素格式转换
        // -------------------------------------------------------------------

        /** @brief RasterPixelFormat → OpenCV type（CV_8UC1 等）。失败返回 -1。 */
        int toCvType(RasterPixelFormat format);

        /** @brief OpenCV type → RasterPixelFormat。失败返回 RASTER_FORMAT_UNKNOWN。 */
        RasterPixelFormat fromCvType(int cvType);

        /** @brief 格式对应的通道数。 */
        int channelCount(RasterPixelFormat format);

        /** @brief 格式对应的单通道字节数。 */
        int bytesPerChannel(RasterPixelFormat format);

        // -------------------------------------------------------------------
        // 通用工具
        // -------------------------------------------------------------------

        /** @brief 校验句柄非空。 */
        bool validImage(RasterImage image);

        /** @brief 校验句柄非空。 */
        bool validContourSet(RasterContourSet set);

        /** @brief 把任意输入统一转成单通道灰度（不修改原图）。 */
        cv::Mat asGray(const cv::Mat& src);

        /** @brief 把任意输入统一转成 8 位单通道（阈值/形态学的前置条件）。 */
        cv::Mat asGray8(const cv::Mat& src);

        /** @brief 形态学核形状 → OpenCV 枚举。 */
        int toMorphShape(int kernelShape);

        /** @brief 边界模式 → OpenCV 枚举。 */
        int toBorderMode(int borderMode);

        /** @brief 插值方式 → OpenCV 枚举。 */
        int toInterpolation(int interpolation);

        /** @brief 生成一个全新的输出图像句柄（已分配好 mat）。 */
        RasterImage makeImage(const cv::Mat& mat);

        /** @brief 把结果矩阵封装成新句柄（统一的成功出口）。
         *
         * 放在头里而不是每个 .cpp 各写一份：unity build 会把多个 .cpp 合进同一个
         * 翻译单元，各写一份的匿名命名空间函数会撞名（redefinition）。
         */
        inline RasterStatus emitImage(const cv::Mat& mat, RasterImage* out)
        {
            if (mat.empty()) {
                setLastError("operation produced an empty image");
                return RASTER_E_INTERNAL;
            }
            *out = makeImage(mat);
            return RASTER_OK;
        }
        /** @brief 统一出口：把异常翻译成状态码（库内任何 OpenCV 调用都可能抛）。 */
        RasterStatus guard(const char* what, const std::function<RasterStatus()>& body);

    }  // namespace internal
}  // namespace raster
