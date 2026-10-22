/**
 * @file RasterStatus.cpp
 * @brief 版本查询、状态文本、线程局部错误信息、通用内部工具。
 */

#include "Core/RasterInternal.h"

#include <exception>

namespace raster
{
    namespace internal
    {
        namespace
        {
            /**
             * 线程局部错误缓冲。
             *
             * 为什么必须线程局部：位图处理是典型的并行场景（多台相机 / 多张图
             * 同时处理）。用全局缓冲会让 A 线程的错误信息被 B 线程覆盖，
             * 现场表现是"报错信息对不上实际失败原因"，比没有错误信息更糟。
             */
            thread_local std::string g_lastError;
        }  // namespace

        void setLastError(const std::string& message)
        {
            g_lastError = message;
        }

        std::string lastError()
        {
            return g_lastError;
        }

        int toCvType(RasterPixelFormat format)
        {
            switch (format) {
                case RASTER_FORMAT_GRAY8:  return CV_8UC1;
                case RASTER_FORMAT_BGR8:   return CV_8UC3;
                case RASTER_FORMAT_BGRA8:  return CV_8UC4;
                case RASTER_FORMAT_GRAY16: return CV_16UC1;
                default:                   return -1;
            }
        }

        RasterPixelFormat fromCvType(int cvType)
        {
            switch (cvType) {
                case CV_8UC1:  return RASTER_FORMAT_GRAY8;
                case CV_8UC3:  return RASTER_FORMAT_BGR8;
                case CV_8UC4:  return RASTER_FORMAT_BGRA8;
                case CV_16UC1: return RASTER_FORMAT_GRAY16;
                default:       return RASTER_FORMAT_UNKNOWN;
            }
        }

        int channelCount(RasterPixelFormat format)
        {
            switch (format) {
                case RASTER_FORMAT_GRAY8:
                case RASTER_FORMAT_GRAY16: return 1;
                case RASTER_FORMAT_BGR8:   return 3;
                case RASTER_FORMAT_BGRA8:  return 4;
                default:                   return 0;
            }
        }

        int bytesPerChannel(RasterPixelFormat format)
        {
            switch (format) {
                case RASTER_FORMAT_GRAY8:
                case RASTER_FORMAT_BGR8:
                case RASTER_FORMAT_BGRA8:  return 1;
                case RASTER_FORMAT_GRAY16: return 2;
                default:                   return 0;
            }
        }

        bool validImage(RasterImage image)
        {
            if (image == nullptr) {
                setLastError("image handle is null");
                return false;
            }
            if (image->mat.empty()) {
                setLastError("image handle carries an empty matrix");
                return false;
            }
            return true;
        }

        bool validContourSet(RasterContourSet set)
        {
            if (set == nullptr) {
                setLastError("contour set handle is null");
                return false;
            }
            return true;
        }

        cv::Mat asGray(const cv::Mat& src)
        {
            if (src.channels() == 1) {
                return src;
            }
            cv::Mat gray;
            cv::cvtColor(src, gray, src.channels() == 4 ? cv::COLOR_BGRA2GRAY : cv::COLOR_BGR2GRAY);
            return gray;
        }

        cv::Mat asGray8(const cv::Mat& src)
        {
            cv::Mat gray = asGray(src);
            if (gray.depth() == CV_8U) {
                return gray;
            }
            if (gray.depth() == CV_16U) {
                cv::Mat out;
                gray.convertTo(out, CV_8U, 1.0 / 257.0);
                return out;
            }
            cv::Mat out;
            gray.convertTo(out, CV_8U);
            return out;
        }

        int toMorphShape(int kernelShape)
        {
            switch (kernelShape) {
                case RASTER_KERNEL_ELLIPSE: return cv::MORPH_ELLIPSE;
                case RASTER_KERNEL_CROSS:   return cv::MORPH_CROSS;
                default:                     return cv::MORPH_RECT;
            }
        }

        int toBorderMode(int borderMode)
        {
            switch (borderMode) {
                case RASTER_BORDER_REPLICATE: return cv::BORDER_REPLICATE;
                case RASTER_BORDER_REFLECT:   return cv::BORDER_REFLECT;
                case RASTER_BORDER_WRAP:      return cv::BORDER_WRAP;
                default:                      return cv::BORDER_CONSTANT;
            }
        }

        int toInterpolation(int interpolation)
        {
            switch (interpolation) {
                case RASTER_INTERP_LINEAR:   return cv::INTER_LINEAR;
                case RASTER_INTERP_CUBIC:    return cv::INTER_CUBIC;
                case RASTER_INTERP_AREA:     return cv::INTER_AREA;
                case RASTER_INTERP_LANCZOS4: return cv::INTER_LANCZOS4;
                default:                     return cv::INTER_NEAREST;
            }
        }

        RasterImage makeImage(const cv::Mat& mat)
        {
            auto* handle = new RasterImageHandle();
            handle->mat = mat;
            return handle;
        }

        RasterStatus guard(const char* what, const std::function<RasterStatus()>& body)
        {
            try {
                const RasterStatus status = body();
                if (status != RASTER_OK) {
                    setLastError(std::string(what) + " failed");
                }
                return status;
            } catch (const cv::Exception& ex) {
                // OpenCV 抛异常是常态（参数非法、内存不足、图像尺寸溢出等）。
                // 直接穿出 DLL 边界会破坏调用方的栈：调用方可能是 C 语言、
                // 可能是 .NET、可能是没有任何异常支持的嵌入式运行时。
                // 因此这里一律翻译成状态码 + 文本。
                setLastError(std::string(what) + ": " + ex.what());
                return RASTER_E_INTERNAL;
            } catch (const std::bad_alloc&) {
                setLastError(std::string(what) + ": out of memory");
                return RASTER_E_OUT_OF_MEMORY;
            } catch (const std::exception& ex) {
                setLastError(std::string(what) + ": " + ex.what());
                return RASTER_E_INTERNAL;
            } catch (...) {
                setLastError(std::string(what) + ": unknown error");
                return RASTER_E_INTERNAL;
            }
        }

    }  // namespace internal
}  // namespace raster

// ===========================================================================
//                            公开 C ABI 实现
// ===========================================================================

extern "C" {

uint32_t RASTER_CALL Raster_GetVersion(void)
{
    return RASTER_VERSION;
}

uint32_t RASTER_CALL Raster_GetAbiVersion(void)
{
    return RASTER_ABI_VERSION;
}

const char* RASTER_CALL Raster_GetVersionString(void)
{
    // 版本串由公开头里的数字宏推导（RASTER_VERSION_STRING），
    // 与 RASTER_VERSION 同源，不会出现"宏说 1.0.0、字符串说 1.0.1"的漂移。
    static const char* kVersionString = RASTER_VERSION_STRING;
    return kVersionString;
}

uint32_t RASTER_CALL Raster_GetOpenCvVersion(void)
{
    return static_cast<uint32_t>(CV_VERSION_MAJOR);
}

const char* RASTER_CALL Raster_StatusText(int32_t status)
{
    switch (status) {
        case RASTER_OK:                  return "OK";
        case RASTER_E_INVALID_ARG:       return "Invalid argument";
        case RASTER_E_OUT_OF_MEMORY:     return "Out of memory";
        case RASTER_E_IO:                return "Input/output error";
        case RASTER_E_UNSUPPORTED:       return "Unsupported operation";
        case RASTER_E_STRUCT_SIZE:       return "Struct size mismatch (ABI mismatch)";
        case RASTER_E_NOT_FOUND:         return "Not found";
        case RASTER_E_BUFFER_TOO_SMALL:  return "Buffer too small";
        case RASTER_E_EMPTY:             return "Empty input";
        case RASTER_E_INTERNAL:          return "Internal error";
        default:                         return "Unknown status";
    }
}

int RASTER_CALL Raster_GetLastErrorMessage(char* buffer, size_t bufferSize)
{
    if (buffer == nullptr || bufferSize == 0) {
        return -1;
    }
    const std::string message = raster::internal::lastError();
    if (message.empty()) {
        buffer[0] = '\0';
        return 0;
    }
    if (message.size() + 1 > bufferSize) {
        // 缓冲区不足：截断写入并返回负值，调用方据此知道需要更大的缓冲。
        const std::size_t copied = bufferSize - 1;
        std::memcpy(buffer, message.data(), copied);
        buffer[copied] = '\0';
        return -static_cast<int>(message.size() + 1);
    }
    std::memcpy(buffer, message.c_str(), message.size() + 1);
    return static_cast<int>(message.size());
}

}  // extern "C"
