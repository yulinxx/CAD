#ifndef CAM_EXPORT_H
#define CAM_EXPORT_H

/**
 * @file CAMExport.h
 * @brief CAM 模块 DLL 导出/导入宏与 ABI 标识。
 *
 * ABI 定位：C++ 内部 ABI（不是 C ABI）。
 *   - 公开头会暴露 std::vector / std::string 等 STL 容器，成员布局与 CRT 实现强相关；
 *   - 使用方必须与 CAM 使用同一工具链构建：同编译器、同 CRT、同 _ITERATOR_DEBUG_LEVEL、
 *     同运行时库选项，否则容器布局或堆归属不一致会直接破坏内存；
 *   - 不承诺跨编译器、跨语言（C / Python / 其它运行时）的二进制兼容；
 *   - 已否决"对外 C ABI"路线；若将来真的出现仓库外消费方，再单独评估切换。
 *
 * 版本标识：
 *   CAM_ABI_VERSION 与 camAbiInfo() 供桥接层在启动时打日志自检。
 *   任何公开结构体的字段增删改（含顺序调整）都必须递增 CAM_ABI_VERSION。
 */

#if defined(_WIN32) || defined(__CYGWIN__)
    #ifdef CAM_EXPORTS
        #define CAM_API __declspec(dllexport)
    #else
        #define CAM_API __declspec(dllimport)
    #endif
#else
    #define CAM_API __attribute__((visibility("default")))
#endif

#include <cstddef>
#include <cstdint>

/// 公开 C++ 内部 ABI 的版本号
#define CAM_ABI_VERSION 1

namespace Cam
{
    /// ABI 自检快照 —— 只含版本号与关键结构 sizeof，查询无运行时开销
    struct CAM_API CamAbiInfo
    {
        int32_t version = 0;
        size_t sizeofPathSegment = 0;
        size_t sizeofContour = 0;
        size_t sizeofEntitySnapshot = 0;
        size_t sizeofToolpathJobInput = 0;
        size_t sizeofCamPlan = 0;
        size_t sizeofCamResult = 0;
    };

    /// 返回当前 CAM 的 ABI 版本与关键结构 sizeof；桥接层启动自检用
    CAM_API CamAbiInfo camAbiInfo();
}

#endif  // CAM_EXPORT_H
