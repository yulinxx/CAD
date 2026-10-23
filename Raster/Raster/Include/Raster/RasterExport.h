#ifndef RASTER_EXPORT_H
#define RASTER_EXPORT_H

/**
 * @file RasterExport.h
 * @brief Raster(位图处理)模块的符号导出 / 导入控制头。
 *
 * 平台差异：
 *   - Windows : __declspec(dllexport) / __declspec(dllimport)
 *   - GCC/Clang: __attribute__((visibility("default")))，其余符号默认隐藏
 *   - 其它    : 空宏
 *
 * 使用约定：
 *   - 只有在**编译 Raster 模块自身**时才定义 RASTER_EXPORTS。
 *     消费方（无论是 CAD 主工程还是第三方程序）都不定义它，从而走 dllimport 分支。
 *   - 该头是**纯 C 头**，可被 C / C++ / 各种 FFI（Python ctypes、Delphi、Go cgo）
 *     安全包含。头中不出现任何 C++ 构造。
 *
 * 重要：本模块对外只暴露 `extern "C"` 的**纯 C ABI**（见 RasterApi.h），
 * 公开头里不出现 std::string / std::vector / cv::Mat / 异常。
 * 原因与取舍见 Docs/09-Raster/Raster模块架构.md §2「STD 导出问题」。
 */

/*
 * RASTER_STATIC：把导出宏退化为空。
 *
 * 用途：单元测试把 Src 直接编进测试可执行文件（白盒测试），此时符号就在
 * 本模块内，若仍走 dllimport 分支，MSVC 会报"符号被声明为 dllimport 但
 * 在本模块内定义"的链接错误。定义 RASTER_STATIC 即可让宏变空。
 */
#if defined(RASTER_STATIC)
    #define RASTER_API
#elif defined(_WIN32) || defined(_WIN64)
    #ifdef RASTER_EXPORTS
        #define RASTER_API __declspec(dllexport)
    #else
        #define RASTER_API __declspec(dllimport)
    #endif
#elif defined(__GNUC__) || defined(__clang__)
    #ifdef RASTER_EXPORTS
        #define RASTER_API __attribute__((visibility("default")))
    #else
        #define RASTER_API
    #endif
#else
    #define RASTER_API
#endif

/* C 链接约定：仅在 C++ 编译单元下生效；纯 C 编译单元天然是 C 链接。 */
#ifdef __cplusplus
    #define RASTER_C extern "C"
#else
    #define RASTER_C
#endif

/**
 * @brief 调用约定占位。
 *
 * 目前所有平台统一使用 **cdecl**：Windows 上 x64 只有一种调用约定，x86 上 cdecl
 * 允许调用方与被调用方用不同的栈清理方式，跨模块更安全。
 * 保留该宏是为了将来若要切换到 stdcall / winapi 时不必改动全部声明。
 */
#define RASTER_CALL

#endif /* RASTER_EXPORT_H */
