#pragma once

/**
 * @file RasterTestSupport.h
 * @brief 位图处理测试的公共支撑：合成图元与句柄 RAII 包装。
 *
 * 所有测试图都是**程序合成**的，不依赖任何外部素材文件：
 * 机器上有没有样图、样图版本对不对，都不该影响测试结果。
 */

#include <Raster/RasterApi.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace raster_test
{
    /** @brief 句柄的 RAII 包装，避免测试里忘记释放导致跨用例串味。 */
    template <typename Handle, void (*Destroy)(Handle)>
    class HandleGuard
    {
    public:
        HandleGuard() = default;
        explicit HandleGuard(Handle handle) : m_handle(handle) {}

        ~HandleGuard() { reset(); }

        HandleGuard(HandleGuard&& other) noexcept : m_handle(other.m_handle)
        {
            other.m_handle = nullptr;
        }

        HandleGuard& operator=(HandleGuard&& other) noexcept
        {
            if (this != &other) {
                reset();
                m_handle = other.m_handle;
                other.m_handle = nullptr;
            }
            return *this;
        }

        HandleGuard(const HandleGuard&) = delete;
        HandleGuard& operator=(const HandleGuard&) = delete;

        Handle get() const { return m_handle; }
        bool valid() const { return m_handle != nullptr; }

        Handle release()
        {
            Handle handle = m_handle;
            m_handle = nullptr;
            return handle;
        }

        void reset(Handle handle = nullptr)
        {
            if (m_handle) {
                Destroy(m_handle);
            }
            m_handle = handle;
        }

    private:
        Handle m_handle = nullptr;
    };

    using ImageGuard = HandleGuard<RasterImage, Raster_DestroyImage>;
    using ContourGuard = HandleGuard<RasterContourSet, Raster_DestroyContourSet>;
    using DefectGuard = HandleGuard<RasterDefectSet, Raster_DestroyDefectSet>;
    using PipelineGuard = HandleGuard<RasterPipeline, Raster_DestroyPipeline>;
    using CodeSetGuard = HandleGuard<RasterCodeSet, Raster_DestroyCodeSet>;

    /** @brief 断言状态码，否则抛出带上下文的异常。 */
    inline void expectOk(RasterStatus status, const char* what)
    {
        if (status != RASTER_OK) {
            throw std::runtime_error(std::string(what) + " failed with status " +
                                     std::to_string(static_cast<int>(status)) + " (" +
                                     Raster_StatusText(static_cast<int32_t>(status)) + ")");
        }
    }

    /** @brief 线性灰度渐变（左暗右亮）。 */
    inline ImageGuard makeGradient(int width, int height)
    {
        RasterImage handle = nullptr;
        expectOk(Raster_CreateImage(width, height, RASTER_FORMAT_GRAY8, &handle), "Raster_CreateImage");
        ImageGuard guard(handle);

        RasterImageView view{};
        view.structSize = sizeof(RasterImageView);
        expectOk(Raster_GetImageView(handle, &view), "Raster_GetImageView");
        auto* pixels = static_cast<uint8_t*>(const_cast<void*>(view.data));
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                pixels[y * view.stride + x] = static_cast<uint8_t>((x * 255) / (width > 1 ? width - 1 : 1));
            }
        }
        return guard;
    }

    /**
     * @brief 黑色背景 + 白色实心矩形 + 矩形内部的黑色圆孔。
     *
     * 用来验证：二值化、轮廓提取（外轮廓 + 孔洞）、面积/周长、割缝补偿。
     *
     * 为什么是这个配色：二值化后"白 = 保留"，整个画面里**只有**一个白色连通域，
     * 它的轮廓层级就是"1 条外轮廓 + 1 条孔洞"，拓扑干净，断言不会因为
     * 背景的连通性而含糊。
     */
    inline ImageGuard makeFrameWithHole(int width, int height, int margin, int frameThickness)
    {
        RasterImage handle = nullptr;
        expectOk(Raster_CreateImage(width, height, RASTER_FORMAT_GRAY8, &handle), "Raster_CreateImage");
        ImageGuard guard(handle);

        RasterImageView view{};
        view.structSize = sizeof(RasterImageView);
        expectOk(Raster_GetImageView(handle, &view), "Raster_GetImageView");
        auto* pixels = static_cast<uint8_t*>(const_cast<void*>(view.data));
        std::memset(pixels, 0, static_cast<size_t>(view.stride) * static_cast<size_t>(height));

        const int x0 = margin;
        const int y0 = margin;
        const int x1 = width - margin - 1;
        const int y1 = height - margin - 1;
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                pixels[y * view.stride + x] = 255;
            }
        }

        // 在白色矩形中间挖一个黑圆孔，形成一条孔洞轮廓。
        const float cx = static_cast<float>(width) * 0.5f;
        const float cy = static_cast<float>(height) * 0.5f;
        const float radius = static_cast<float>(std::min(width, height)) * 0.2f;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const float dx = static_cast<float>(x) - cx;
                const float dy = static_cast<float>(y) - cy;
                if (dx * dx + dy * dy <= radius * radius) {
                    pixels[y * view.stride + x] = 0;
                }
            }
        }
        (void)frameThickness;
        return guard;
    }

    /** @brief 棋盘格（用于缩放/插值的结构性检查）。 */
    inline ImageGuard makeCheckerboard(int width, int height, int cell)
    {
        RasterImage handle = nullptr;
        expectOk(Raster_CreateImage(width, height, RASTER_FORMAT_GRAY8, &handle), "Raster_CreateImage");
        ImageGuard guard(handle);

        RasterImageView view{};
        view.structSize = sizeof(RasterImageView);
        expectOk(Raster_GetImageView(handle, &view), "Raster_GetImageView");
        auto* pixels = static_cast<uint8_t*>(const_cast<void*>(view.data));
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const bool on = ((x / cell) + (y / cell)) % 2 == 0;
                pixels[y * view.stride + x] = on ? 255 : 0;
            }
        }
        return guard;
    }

    /** @brief 取某个像素的灰度值。 */
    inline int pixelAt(RasterImage image, int x, int y)
    {
        RasterImageView view{};
        view.structSize = sizeof(RasterImageView);
        expectOk(Raster_GetImageView(image, &view), "Raster_GetImageView");
        const auto* pixels = static_cast<const uint8_t*>(view.data);
        return pixels[y * view.stride + x];
    }

    /** @brief 统计某个图像里值为 255 的像素个数。 */
    inline int countWhite(RasterImage image)
    {
        RasterImageView view{};
        view.structSize = sizeof(RasterImageView);
        expectOk(Raster_GetImageView(image, &view), "Raster_GetImageView");
        const auto* pixels = static_cast<const uint8_t*>(view.data);
        int count = 0;
        for (int y = 0; y < view.height; ++y) {
            for (int x = 0; x < view.width; ++x) {
                if (pixels[y * view.stride + x] == 255) {
                    ++count;
                }
            }
        }
        return count;
    }

    /** @brief 构造一个管线算子参数块。 */
    inline RasterOpParams makeOp(int32_t opId)
    {
        RasterOpParams params{};
        params.structSize = sizeof(RasterOpParams);
        params.opId = opId;
        return params;
    }

}  // namespace raster_test
