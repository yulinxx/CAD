#ifndef RASTER_CPP_H
#define RASTER_CPP_H

/**
 * @file RasterCpp.h
 * @brief Raster 模块的**纯头文件** C++ 门面。
 *
 * ============================================================================
 * 为什么有这一层
 * ============================================================================
 *
 * C ABI 是给"任何语言都能调用"的契约，代价是：
 *   - 路径要传 `const char*`；
 *   - 每个句柄都要手写 create/destroy，成对出错就是泄漏；
 *   - 拿结果要自己准备缓冲区。
 *
 * 这一层用 C++ 重新包装一遍：std::string 传路径、RAII 管句柄、std::vector 收结果。
 *
 * ============================================================================
 * ★ 关键点：STL **不会**跨越 DLL 边界
 * ============================================================================
 *
 * 本文件里的所有类**全部是 header-only inline**：没有一行代码被编进 DLL，
 * 也没有任何一个带 RASTER_API 的符号。所有 std::string / std::vector /
 * std::unique_ptr 都只存在于**调用方的翻译单元**里，生命周期完全由调用方掌控。
 *
 * 这正是"STD 导出问题"的正解：
 *   - 如果在 DLL 里导出一个返回 std::string 的函数，MSVC 会报 C4251
 *     （'type' needs to have dll-interface to be used by clients of class），
 *     而且更危险的是——调用方与 DLL 的 CRT / 编译器 / _ITERATOR_DEBUG_LEVEL
 *     不一致时，std::string 的内部布局不一致，**释放时崩在另一个模块里**，
 *     现场表现是"程序用着用着随机崩在 free()"，极难定位。
 *   - 把 STL 全部关在头文件里，就绕开了整个问题：
 *     DLL 只暴露 POD + 句柄，跨边界的每一个字节都有确定解释。
 *
 * 代价：这些 inline 函数在每个使用方 TU 里都要重新编译一次。
 * 对一个胶水层而言，这个代价可以接受。
 *
 * ============================================================================
 * 异常
 * ============================================================================
 *
 * 本层的 `check()` 在失败时抛 std::runtime_error。**这个异常完全在调用方的
 * 栈上产生并被捕获，不会穿过 DLL 边界**，因此不存在"跨模块异常"问题。
 * 需要 no-throw 语义时，请直接调用 RasterApi.h 里的 C 函数。
 *
 * 链接：需要 C++11 以上的运行时。使用本文件不会引入任何额外链接依赖。
 */

#include "RasterApi.h"

#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace raster
{

    // ---------------------------------------------------------------------
    // 错误处理
    // ---------------------------------------------------------------------

    /** @brief 状态码 → 文本（静态存储，永不为空）。 */
    inline const char* statusText(RasterStatus status)
    {
        return Raster_StatusText(static_cast<int32_t>(status));
    }

    /** @brief 取最近一次失败的详细消息。 */
    inline std::string lastErrorMessage()
    {
        char buffer[1024] = {0};
        const int written = Raster_GetLastErrorMessage(buffer, sizeof(buffer));
        if (written <= 0) {
            return std::string();
        }
        return std::string(buffer, static_cast<std::size_t>(written));
    }

    /** @brief 失败即抛异常。异常在调用方栈上产生，不跨越 DLL 边界。 */
    inline void check(RasterStatus status, const char* what)
    {
        if (status == RASTER_OK) {
            return;
        }
        std::string message(what ? what : "Raster operation failed");
        message += ": ";
        message += statusText(status);
        const std::string detail = lastErrorMessage();
        if (!detail.empty()) {
            message += " (";
            message += detail;
            message += ")";
        }
        throw std::runtime_error(message);
    }

    // ---------------------------------------------------------------------
    // 图像
    // ---------------------------------------------------------------------

    /**
     * @brief 图像句柄的 RAII 包装（**只移动，不可拷贝**）。
     */
    class Image
    {
    public:
        Image() = default;

        explicit Image(RasterImage handle) : m_handle(handle) {}

        ~Image()
        {
            release();
        }

        Image(Image&& other) noexcept : m_handle(other.m_handle)
        {
            other.m_handle = nullptr;
        }

        Image& operator=(Image&& other) noexcept
        {
            if (this != &other) {
                release();
                m_handle = other.m_handle;
                other.m_handle = nullptr;
            }
            return *this;
        }

        Image(const Image&) = delete;
        Image& operator=(const Image&) = delete;

        void reset(RasterImage handle = nullptr)
        {
            release();
            m_handle = handle;
        }

        RasterImage get() const { return m_handle; }
        bool valid() const { return m_handle != nullptr; }

        void release()
        {
            if (m_handle) {
                Raster_DestroyImage(m_handle);
                m_handle = nullptr;
            }
        }

        // ---- 元信息 ----

        int32_t width() const
        {
            RasterImageInfo info{};
            info.structSize = sizeof(RasterImageInfo);
            check(Raster_GetImageInfo(m_handle, &info), "Raster_GetImageInfo");
            return info.width;
        }

        int32_t height() const
        {
            RasterImageInfo info{};
            info.structSize = sizeof(RasterImageInfo);
            check(Raster_GetImageInfo(m_handle, &info), "Raster_GetImageInfo");
            return info.height;
        }

        int32_t channels() const
        {
            RasterImageInfo info{};
            info.structSize = sizeof(RasterImageInfo);
            check(Raster_GetImageInfo(m_handle, &info), "Raster_GetImageInfo");
            return info.channels;
        }

        RasterPixelFormat format() const
        {
            RasterImageInfo info{};
            info.structSize = sizeof(RasterImageInfo);
            check(Raster_GetImageInfo(m_handle, &info), "Raster_GetImageInfo");
            return static_cast<RasterPixelFormat>(info.format);
        }

        /** @brief 借用视图（只读）：data 指向库内内存，随句柄失效。 */
        RasterImageView view() const
        {
            RasterImageView v{};
            v.structSize = sizeof(RasterImageView);
            check(Raster_GetImageView(m_handle, &v), "Raster_GetImageView");
            return v;
        }

        /** @brief 把像素拷进 std::vector（紧凑排列）。 */
        std::vector<uint8_t> toBytes() const
        {
            RasterImageInfo info{};
            info.structSize = sizeof(RasterImageInfo);
            check(Raster_GetImageInfo(m_handle, &info), "Raster_GetImageInfo");
            std::vector<uint8_t> buffer(info.dataSize);
            check(Raster_CopyImageTo(m_handle, buffer.data(), buffer.size(), 0),
                  "Raster_CopyImageTo");
            return buffer;
        }

        // ---- 工厂 ----

        static Image create(int32_t w, int32_t h, RasterPixelFormat format)
        {
            RasterImage handle = nullptr;
            check(Raster_CreateImage(w, h, static_cast<int32_t>(format), &handle),
                  "Raster_CreateImage");
            return Image(handle);
        }

        static Image fromBytes(int32_t w, int32_t h, RasterPixelFormat format,
                               const void* data, std::size_t size)
        {
            RasterImageView v{};
            v.structSize = sizeof(RasterImageView);
            v.width = w;
            v.height = h;
            v.channels = (format == RASTER_FORMAT_GRAY8 || format == RASTER_FORMAT_GRAY16) ? 1
                                                                                          : (format == RASTER_FORMAT_BGRA8 ? 4 : 3);
            v.format = static_cast<int32_t>(format);
            v.data = data;
            v.stride = 0;
            const std::size_t needed = static_cast<std::size_t>(v.width) * v.height * v.channels;
            if (data == nullptr || size < needed) {
                throw std::runtime_error("raster::Image::fromBytes: buffer too small");
            }
            RasterImage handle = nullptr;
            check(Raster_CreateImageFromView(&v, &handle), "Raster_CreateImageFromView");
            return Image(handle);
        }

        static Image load(const std::string& path)
        {
            RasterImage handle = nullptr;
            check(Raster_LoadImage(path.c_str(), &handle), "Raster_LoadImage");
            return Image(handle);
        }

        // ---- 输出 ----

        void save(const std::string& path) const
        {
            check(Raster_SaveImage(m_handle, path.c_str()), "Raster_SaveImage");
        }

        void saveAs(const std::string& path, RasterFileFormat format, int32_t quality = 0) const
        {
            check(Raster_SaveImageAs(m_handle, path.c_str(), static_cast<int32_t>(format), quality),
                  "Raster_SaveImageAs");
        }

        // ---- 处理（全部返回新图，不修改自身） ----

        Image toGray() const
        {
            RasterImage out = nullptr;
            check(Raster_ToGray(m_handle, &out), "Raster_ToGray");
            return Image(out);
        }

        Image invert() const
        {
            RasterImage out = nullptr;
            check(Raster_Invert(m_handle, &out), "Raster_Invert");
            return Image(out);
        }

        Image threshold(RasterThresholdMethod method, double thresh, double maxValue) const
        {
            RasterImage out = nullptr;
            check(Raster_Threshold(m_handle, static_cast<int32_t>(method), thresh, maxValue, &out),
                  "Raster_Threshold");
            return Image(out);
        }

        Image dither(RasterDitherMethod method) const
        {
            RasterImage out = nullptr;
            check(Raster_Dither(m_handle, static_cast<int32_t>(method), &out), "Raster_Dither");
            return Image(out);
        }

        Image resize(double fx, double fy, RasterInterpolation interpolation) const
        {
            RasterImage out = nullptr;
            check(Raster_Resize(m_handle, fx, fy, static_cast<int32_t>(interpolation), &out),
                  "Raster_Resize");
            return Image(out);
        }

        Image crop(int32_t x, int32_t y, int32_t w, int32_t h) const
        {
            RasterImage out = nullptr;
            check(Raster_Crop(m_handle, x, y, w, h, &out), "Raster_Crop");
            return Image(out);
        }

        Image rotate(double angleDeg, RasterBorderMode borderMode = RASTER_BORDER_CONSTANT,
                     double borderValue = 0.0) const
        {
            RasterImage out = nullptr;
            check(Raster_Rotate(m_handle, angleDeg, static_cast<int32_t>(borderMode), borderValue, &out),
                  "Raster_Rotate");
            return Image(out);
        }

    private:
        RasterImage m_handle = nullptr;
    };

    // ---------------------------------------------------------------------
    // 轮廓集合
    // ---------------------------------------------------------------------

    /** @brief 轮廓集合的 RAII 包装（只移动）。 */
    class ContourSet
    {
    public:
        ContourSet() = default;

        explicit ContourSet(RasterContourSet handle) : m_handle(handle) {}

        ~ContourSet()
        {
            release();
        }

        ContourSet(ContourSet&& other) noexcept : m_handle(other.m_handle)
        {
            other.m_handle = nullptr;
        }

        ContourSet& operator=(ContourSet&& other) noexcept
        {
            if (this != &other) {
                release();
                m_handle = other.m_handle;
                other.m_handle = nullptr;
            }
            return *this;
        }

        ContourSet(const ContourSet&) = delete;
        ContourSet& operator=(const ContourSet&) = delete;

        void release()
        {
            if (m_handle) {
                Raster_DestroyContourSet(m_handle);
                m_handle = nullptr;
            }
        }

        RasterContourSet get() const { return m_handle; }
        bool valid() const { return m_handle != nullptr; }

        int32_t count() const
        {
            int32_t n = 0;
            check(Raster_ContourSetCount(m_handle, &n), "Raster_ContourSetCount");
            return n;
        }

        /** @brief 取第 index 条轮廓的顶点（x,y 交替）。 */
        std::vector<double> points(int32_t index) const
        {
            RasterContour contour{};
            contour.structSize = sizeof(RasterContour);
            check(Raster_ContourSetGet(m_handle, index, &contour), "Raster_ContourSetGet");
            return std::vector<double>(contour.points,
                                       contour.points + static_cast<std::size_t>(contour.pointCount) * 2);
        }

        RasterContourInfo info(int32_t index) const
        {
            RasterContourInfo info{};
            info.structSize = sizeof(RasterContourInfo);
            check(Raster_ContourSetGetInfo(m_handle, index, &info), "Raster_ContourSetGetInfo");
            return info;
        }

        /** @brief 割缝补偿。 */
        ContourSet kerfCompensate(double kerfWidth, RasterKerfSide side) const
        {
            RasterContourSet out = nullptr;
            check(Raster_KerfCompensate(m_handle, kerfWidth, static_cast<int32_t>(side), &out),
                  "Raster_KerfCompensate");
            return ContourSet(out);
        }

        /** @brief 走刀顺序排序。 */
        ContourSet ordered(RasterContourOrder mode) const
        {
            RasterContourSet out = nullptr;
            check(Raster_OrderContours(m_handle, static_cast<int32_t>(mode), &out),
                  "Raster_OrderContours");
            return ContourSet(out);
        }

        static ContourSet trace(const Image& image, RasterTraceMode mode, double minArea,
                                double approxEpsilon)
        {
            RasterContourSet out = nullptr;
            check(Raster_TraceContours(image.get(), static_cast<int32_t>(mode), minArea,
                                       approxEpsilon, &out),
                  "Raster_TraceContours");
            return ContourSet(out);
        }

    private:
        RasterContourSet m_handle = nullptr;
    };

    // ---------------------------------------------------------------------
    // 缺陷集合
    // ---------------------------------------------------------------------

    /** @brief 缺陷集合的 RAII 包装（只移动）。 */
    class DefectSet
    {
    public:
        DefectSet() = default;
        explicit DefectSet(RasterDefectSet handle) : m_handle(handle) {}

        ~DefectSet() { release(); }

        DefectSet(DefectSet&& other) noexcept : m_handle(other.m_handle)
        {
            other.m_handle = nullptr;
        }

        DefectSet& operator=(DefectSet&& other) noexcept
        {
            if (this != &other) {
                release();
                m_handle = other.m_handle;
                other.m_handle = nullptr;
            }
            return *this;
        }

        DefectSet(const DefectSet&) = delete;
        DefectSet& operator=(const DefectSet&) = delete;

        void release()
        {
            if (m_handle) {
                Raster_DestroyDefectSet(m_handle);
                m_handle = nullptr;
            }
        }

        RasterDefectSet get() const { return m_handle; }
        bool valid() const { return m_handle != nullptr; }

        int32_t count() const
        {
            int32_t n = 0;
            check(Raster_DefectSetCount(m_handle, &n), "Raster_DefectSetCount");
            return n;
        }

        RasterDefect at(int32_t index) const
        {
            RasterDefect defect{};
            defect.structSize = sizeof(RasterDefect);
            check(Raster_DefectSetGet(m_handle, index, &defect), "Raster_DefectSetGet");
            return defect;
        }

        static DefectSet detect(const Image& image, double sensitivity, double minArea)
        {
            RasterDefectSet out = nullptr;
            check(Raster_DetectDefects(image.get(), sensitivity, minArea, &out),
                  "Raster_DetectDefects");
            return DefectSet(out);
        }

    private:
        RasterDefectSet m_handle = nullptr;
    };

    // ---------------------------------------------------------------------
    // 管线
    // ---------------------------------------------------------------------

    /** @brief 码集合的 RAII 包装（只移动）。 */
    class CodeSet
    {
    public:
        CodeSet() = default;
        explicit CodeSet(RasterCodeSet handle) : m_handle(handle) {}

        ~CodeSet() { release(); }

        CodeSet(CodeSet&& other) noexcept : m_handle(other.m_handle)
        {
            other.m_handle = nullptr;
        }

        CodeSet& operator=(CodeSet&& other) noexcept
        {
            if (this != &other) {
                release();
                m_handle = other.m_handle;
                other.m_handle = nullptr;
            }
            return *this;
        }

        CodeSet(const CodeSet&) = delete;
        CodeSet& operator=(const CodeSet&) = delete;

        void release()
        {
            if (m_handle) {
                Raster_DestroyCodeSet(m_handle);
                m_handle = nullptr;
            }
        }

        RasterCodeSet get() const { return m_handle; }
        bool valid() const { return m_handle != nullptr; }

        int32_t count() const
        {
            int32_t n = 0;
            check(Raster_CodeSetCount(m_handle, &n), "Raster_CodeSetCount");
            return n;
        }

        RasterCode at(int32_t index) const
        {
            RasterCode code{};
            code.structSize = sizeof(RasterCode);
            check(Raster_CodeSetGet(m_handle, index, &code), "Raster_CodeSetGet");
            return code;
        }

        static CodeSet detect(const Image& image)
        {
            RasterCodeSet out = nullptr;
            check(Raster_DetectCodes(image.get(), &out), "Raster_DetectCodes");
            return CodeSet(out);
        }

    private:
        RasterCodeSet m_handle = nullptr;
    };

    /** @brief 处理管线的 RAII 包装（只移动）。 */
    class Pipeline
    {
    public:
        Pipeline() = default;
        explicit Pipeline(RasterPipeline handle) : m_handle(handle) {}

        ~Pipeline() { release(); }

        Pipeline(Pipeline&& other) noexcept : m_handle(other.m_handle)
        {
            other.m_handle = nullptr;
        }

        Pipeline& operator=(Pipeline&& other) noexcept
        {
            if (this != &other) {
                release();
                m_handle = other.m_handle;
                other.m_handle = nullptr;
            }
            return *this;
        }

        Pipeline(const Pipeline&) = delete;
        Pipeline& operator=(const Pipeline&) = delete;

        void release()
        {
            if (m_handle) {
                Raster_DestroyPipeline(m_handle);
                m_handle = nullptr;
            }
        }

        RasterPipeline get() const { return m_handle; }
        bool valid() const { return m_handle != nullptr; }

        /** @brief 追加算子。params 会被库深拷贝。 */
        Pipeline& add(RasterOpId opId, const RasterOpParams& params)
        {
            check(Raster_PipelineAdd(m_handle, static_cast<int32_t>(opId), &params,
                                      sizeof(RasterOpParams)),
                  "Raster_PipelineAdd");
            return *this;
        }

        int32_t count() const
        {
            int32_t n = 0;
            check(Raster_PipelineCount(m_handle, &n), "Raster_PipelineCount");
            return n;
        }

        Image execute(const Image& src) const
        {
            RasterImage out = nullptr;
            check(Raster_PipelineExecute(m_handle, src.get(), &out), "Raster_PipelineExecute");
            return Image(out);
        }

        static Pipeline create()
        {
            RasterPipeline handle = nullptr;
            check(Raster_CreatePipeline(&handle), "Raster_CreatePipeline");
            return Pipeline(handle);
        }

    private:
        RasterPipeline m_handle = nullptr;
    };

    // ---------------------------------------------------------------------
    // 自由函数
    // ---------------------------------------------------------------------

    /**
     * @brief 构造一个管线算子参数块（已填好 structSize 与 opId）。
     *
     * 字段语义逐算子见 RasterTypes.h 的 RasterOpParams 注释表。
     */
    inline RasterOpParams makeOpParams(RasterOpId opId)
    {
        RasterOpParams params{};
        params.structSize = sizeof(RasterOpParams);
        params.opId = static_cast<int32_t>(opId);
        return params;
    }

    inline std::string versionString() { return std::string(Raster_GetVersionString()); }

    inline uint32_t version() { return Raster_GetVersion(); }

    inline uint32_t abiVersion() { return Raster_GetAbiVersion(); }

    inline RasterStats stats(const Image& image)
    {
        RasterStats s{};
        s.structSize = sizeof(RasterStats);
        check(Raster_ComputeStats(image.get(), &s), "Raster_ComputeStats");
        return s;
    }

    inline RasterCompareResult compare(const Image& a, const Image& b)
    {
        RasterCompareResult r{};
        r.structSize = sizeof(RasterCompareResult);
        check(Raster_CompareImages(a.get(), b.get(), &r), "Raster_CompareImages");
        return r;
    }

    /**
     * @brief 校验图中的码内容是否与期望一致（激光打标防错）。
     * @return true = 找到且内容一致；false = 找到但内容不一致。
     * @throw std::runtime_error 图里没有可解码的码。
     */
    inline bool verifyCode(const Image& image, const std::string& expected)
    {
        int32_t matched = 0;
        const RasterStatus status = Raster_VerifyCode(image.get(), expected.c_str(), &matched);
        if (status == RASTER_E_NOT_FOUND) {
            throw std::runtime_error("raster::verifyCode: no decodable code found in the image");
        }
        check(status, "Raster_VerifyCode");
        return matched != 0;
    }

}  // namespace raster

#endif /* RASTER_CPP_H */
