#pragma once

/**
 * @file MotionPlanCompiler.h
 * @brief 把 2D 文档编译成设备无关的 MotionPlan 字节码。
 *
 * ==================== 它在链路里的位置 ====================
 *
 *   2D 文档（SyEntity + 图层）
 *        ↓  Eg::Cam::GeometryExtractor（唯一几何提取入口，Engine2D/Cam/）
 *        ↓  Eg::Cam::Contour / PathSegment（加工几何）
 *        ↓  本编译器（几何 → 设备无关字节码）
 *   Hw::MotionPlan（设备无关 POD 字节码）
 *        ↓  ProcessingJobService
 *   IMotionCard::loadPlan / IGalvoCard::loadPlan
 *        ↓  各厂商适配器
 *   真机
 *
 * 编译器**不认识任何设备**，设备也不认识几何。能力差异（不支持圆弧、坐标单位是
 * bits 而不是 mm）全部由适配器吸收，这是 MotionPlan IR 存在的全部意义。
 *
 * ==================== 几何从哪来：唯一入口 ====================
 *
 * 几何提取**只走** `Eg::Cam::GeometryExtractor`。它与旧的 `Geo2DPath::decompose`
 * 的区别是决定性的：
 *
 *   Geo2DPath 是**拾取用**的几何 —— 位图返回四个角点、二维码返回包围盒、
 *   色块返回三角剖分的每一条边。用它做加工的后果：
 *     图片被烧成矩形框、二维码被烧成矩形框、色块被烧成一张乱麻。
 *
 *   GeometryExtractor 是**加工用**的几何 —— 图片标记为 Raster（待实现）、
 *   二维码给出真正的模块轮廓、文字给出字形轮廓、色块给出边界环。
 *   不支持的类型返回**明确的 Unsupported + 原因**，绝不静默产出空几何。
 *
 * ==================== 坐标换算：只在这里发生一次 ====================
 *
 * 编译器的最后一步把用户空间（X 右 Y 上、左下原点）换算成机器空间
 * （`Eg::MachineFrame`：台面尺寸 + 原点角点）。适配器与作业服务都**不再**换算。
 *
 * 之前的状态是「UI2D 的旧链路在服务层换算、新 HAL 链路完全不换算」——
 * 两套坐标语义并存，同一份文档走不同路径会烧到不同位置。
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
     * @param spec    作业规格（工艺参数、选集、容差、跳过规则、机器坐标系）
     * @param out     输出。函数内部会先 clear()，失败时其内容不保证有意义
     *
     * 失败条件（一律返回 ok=false 并给出可展示的 error，不静默产出空计划）：
     *   - 工艺参数非法（功率越界、速度非正、遍数 < 1）
     *   - 没有任何可加工图元（全被隐藏/锁定/过滤掉，或选集为空）
     *   - 存在「这类图元暂时无法加工」且 spec.failOnUnmachinableEntity 为 true
     *     （此时 error 会列出类型与数量，见 unmachinableDetails）
     * 「空计划」如果被当成成功返回，上层会照常点亮「开始加工」，
     * 操作者按下去什么都不发生，且没有任何错误提示 —— 最难排查的一类现场问题。
     */
    ToolpathCompileResult compile(const Eg::SceneManager& scene,
                                  const LayerManager* layers,
                                  const ToolpathJobSpec& spec,
                                  Hw::MotionPlanBuilder& out);
}

#endif  // ENABLE_HARDWARE
