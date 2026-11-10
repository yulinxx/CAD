#pragma once

/**
 * @file CamInputBridge.h
 * @brief Engine2D 类型 → CAM ToolpathJobInput 的唯一桥接点。
 *
 * 这是整个仓库里**唯一**允许同时接触 Engine2D 类型和 CAM 类型的代码：
 *   - 输入：Eg::SceneManager + LayerManager + ToolpathJobSpec
 *   - 输出：Cam::ToolpathJobInput（中性 C++ 值类型，属 CAM 的 C++ 内部 ABI）
 *
 * CAM 模块本身（CAM/CAM/Include/CAM/ 下的头文件）不得 include Engine2D 头 ——
 * 所有 Engine2D → CAM 的类型转换工作必须在此文件完成。
 *
 * 条件编译：仅在 BUILD_CAM=ON 时编进 Main。
 */

#include "CAM/ToolpathJobInput.h"

#include <memory>

#ifdef ENABLE_HARDWARE
    // Hardware 头只在编译期有 ENABLE_HARDWARE 时可见
#endif

// ---- Engine2D 类型前置声明（不 include 头文件）----
namespace Eg
{
    class SceneManager;
}

// LayerManager 定义在全局命名空间（Engine2D/Interaction/LayerManager.h）
class LayerManager;
struct ToolpathJobSpec;

namespace CamBridge
{
    /// 把 2D 场景转换成 CAM 的中性输入。
    /// 唯一桥接点：Engine2D 类型在这里被消化，之后 CAM 不再向上感知。
    ///
    /// @param scene     2D 场景（只读，调用方保证主线程）
    /// @param layers    图层管理器（可为 nullptr）
    /// @param spec      作业规格（工艺参数 + 选集 + 跳过规则 + 机器坐标系）
    /// @return          完整的 ToolpathJobInput；jobId 从 spec 带过来
    Cam::ToolpathJobInput buildJobInput(
        const Eg::SceneManager& scene,
        const LayerManager* layers,
        const ToolpathJobSpec& spec);

    /// ABI 自检：打印 CAM 的 ABI 版本号与关键结构 sizeof。
    /// 只在首次调用时输出，用于确认 CAM.dll 与宿主使用同一工具链构建。
    void logAbiSelfCheck();

}  // namespace CamBridge
