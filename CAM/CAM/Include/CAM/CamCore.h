#ifndef CAM_CORE_H
#define CAM_CORE_H

/**
 * @file CamCore.h
 * @brief CAM 编译主入口 —— 纯函数，可在任意线程调用。
 *
 *   ToolpathJobInput → compile → CamPlan + CamResult
 *
 * 线程约束：
 *   - 中性 C++ 值类型输入/输出（含 STL 容器，属 CAM 的 C++ 内部 ABI），
 *     不依赖 Qt / OpenGL / Engine2D
 *   - 内部只用 std::vector + 数学计算，天然线程安全（只读输入）
 *   - 长耗时任务通过 Progress 结构体上报进度与接受取消
 *
 * M0 阶段 compile() 做的事：
 *   1. 输入校验（参数合法性、空作业检测、selectionOnly 边界）
 *   2. 按 LayerBucket 顺序分层处理
 *   3. 每层内：遍历 EntitySnapshot，提取 path → PlanContour
 *   4. 给每条 PlanContour 前面插一条 Rapid（空移）段
 *   5. 轮廓按 sourceEntityId 排序（M0 暂不做最近邻优化）
 *   6. 统计长度（切削/空移/累计）
 *   7. 计算包围盒
 *
 * M0 不做：割缝补偿、引线、穿孔、功率曲线、拐角、共边切割 ——
 * 这些由 M1 新增子模块负责，compile() 的 pipeline 骨架已预留挂载点。
 */

#include "CAM/CAMExport.h"
#include "CAM/ToolpathJobInput.h"
#include "CAM/CamPlan.h"
#include "CAM/CamContext.h"

namespace Cam
{
    /// 编译入口 —— 唯一的公开 API
    ///
    /// @param input     作业输入（中性 C++ 值类型，只读）
    /// @param plan      输出 CamPlan（内部会先 clear()）
    /// @param progress  可选进度/取消结构体（可为 nullptr）
    /// @param result    可选编译结果（错误码 + 统计，可为 nullptr）
    ///
    /// 返回 CamResult.ok 与 result->ok 一致；两者都可用来判断成败。
    CAM_API CamResult compile(const ToolpathJobInput& input,
                              CamPlan& plan,
                              Progress* progress = nullptr,
                              CamResult* result = nullptr);

}  // namespace Cam

#endif  // CAM_CORE_H
