#ifndef CAM_CONTEXT_H
#define CAM_CONTEXT_H

/**
 * @file CamContext.h
 * @brief CAM 编译的上下文：错误码、进度回调、取消标志。
 *
 * 本文件属于 CAM 的 C++ 内部 ABI（见 CAMExport.h）：结构体含 STL 容器，
 * 使用方必须与 CAM 同工具链构建；不保留 structSize 版本守卫字段，
 * 版本一致性由 CAM_ABI_VERSION 与 camAbiInfo() 启动自检保证。
 * 字段只允许尾部追加，不得调整已有字段顺序。
 */

#include "CAM/CAMExport.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Cam
{
    /// CAM 编译结果错误码 —— 与 CamResult.error 配合使用
    enum class CAM_API CamError : int32_t
    {
        Ok = 0,

        // ---- 输入校验 ----
        InvalidParams = -1001,       ///< ProcessParams.valid() 返回 false
        EmptyJob = -1002,            ///< Job 没有任何可加工内容
        ChordToleranceTooLarge = -1003,  ///< 弦高容差超出合理上界（防单位填错）
        SelectionEmpty = -1004,      ///< selectionOnly=true 但没有任何选中

        // ---- 编译流程 ----
        Cancelled = -2001,           ///< 用户中途取消

        // ---- 边界与物理 ----
        UncontainedRaster = -3001,   ///< Fillable/Raster 图元暂不支持，failOnUnmachinable=true 时报错
    };

    /// 长耗时任务的进度与取消机制
    /// 线程安全约定：caller 在任意线程设置 cancelled=1；worker 在后台轮询。
    /// 回调节点：caller 可在 onProgress 里自己做线程转发。
    struct CAM_API Progress
    {
        /// 0.0 ~ 1.0，worker 在每个可取消阶段入口更新
        volatile double fraction = 0.0;

        /// 阶段描述（UTF-8 静态字符串或调用方持有的 char*）
        const char* stage = "";

        /// 置 1 时 worker 应尽快从 compile 返回（ok=false + error=Cancelled）
        volatile int32_t cancelled = 0;

        /// 可选回调。只在后台线程调用。
        /// 返回非 0 可额外请求取消（与 cancelled=1 效果相同）
        int32_t (*onProgress)(double fraction, const char* stage, void* userData) = nullptr;
        void* userData = nullptr;
    };

    /// compile 完整返回值 —— 含 CamError 枚举 + 可读文本
    struct CAM_API CamResult
    {
        bool ok = false;
        CamError error = CamError::Ok;

        /// 可读错误文本（UTF-8），不超过 255 字节 + 结尾 0
        /// 静态字符数组：跨 DLL 安全，避免 std::string 堆分配
        char errorText[256] = {};

        // ---- 统计 ----
        int32_t layerCount = 0;
        int32_t entityCount = 0;
        int32_t contourCount = 0;
        int32_t skippedEntityCount = 0;
        int32_t unmachinableEntityCount = 0;

        /// 无法加工图元的可读清单（类别 + 提取器给出的原因），最多若干条。
        /// 由桥接层透传 GeometryExtractor 的原因，避免用户看到「静默少烧一块」
        std::vector<std::string> unmachinableDetails;
    };

    /// CamError → 可读英文/中文（头文件注释写中文，实际返回英文）
    CAM_API const char* camErrorName(CamError error);

}  // namespace Cam

#endif  // CAM_CONTEXT_H
