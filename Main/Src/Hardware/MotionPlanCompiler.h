#pragma once

/**
 * @file MotionPlanCompiler.h
 * @brief 把 2D 文档编译成设备无关的 MotionPlan 字节码。
 *
 * ==================== 它在链路里的位置 ====================
 *
 *   2D 文档（SyEntity + 图层）
 *        ↓  本编译器（唯一认识几何的地方）
 *   Hw::MotionPlan（设备无关 POD 字节码）
 *        ↓  ProcessingJobService
 *   IMotionCard::loadPlan / IGalvoCard::loadPlan
 *        ↓  各厂商适配器
 *   真机
 *
 * 编译器**不认识任何设备**，设备也不认识几何。能力差异（不支持圆弧、坐标单位是
 * bits 而不是 mm）全部由适配器吸收，这是 MotionPlan IR 存在的全部意义。
 *
 * ==================== 复用而不重写离散化 ====================
 *
 * 曲线离散一律走 `Eg::Geo2DPath::decompose()`：它把任意图元拆成
 * Line / Arc / Ellipse / PolylineApprox 段，于是圆弧能**原样**编译成
 * `MotionOp::ArcTo`，而不是在这里先拉直、再让振镜卡去拟合。
 * 自己写离散化等于把 Engine2D 已经调好的容差逻辑复制一份，迟早对不上。
 */

#ifdef ENABLE_HARDWARE

#include "Hardware/Device/MotionPlan.h"

#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/Interaction/LayerManager.h"

#include "ToolpathJobSpec.h"

namespace MotionPlanCompiler
{
    /**
     * @brief 编译整个文档（或选集）为一段 MotionPlan。
     * @param scene   2D 场景（只读；调用方必须在主线程调用，见 SceneManager 的线程约定）
     * @param layers  图层管理器；可为 nullptr，此时所有图元视为同一图层
     * @param spec    作业规格（工艺参数、选集、容差、跳过规则）
     * @param out     输出。函数内部会先 clear()，失败时其内容不保证有意义
     *
     * 失败条件（一律返回 ok=false 并给出可展示的 error，不静默产出空计划）：
     *   - 工艺参数非法（功率越界、速度非正、遍数 < 1）
     *   - 没有任何可加工图元（全被隐藏/锁定/过滤掉，或选集为空）
     * 「空计划」如果被当成成功返回，上层会照常点亮「开始加工」，
     * 操作者按下去什么都不发生，且没有任何错误提示 —— 最难排查的一类现场问题。
     */
    ToolpathCompileResult compile(const Eg::SceneManager& scene,
                                  const LayerManager* layers,
                                  const ToolpathJobSpec& spec,
                                  Hw::MotionPlanBuilder& out);
}

#endif  // ENABLE_HARDWARE
