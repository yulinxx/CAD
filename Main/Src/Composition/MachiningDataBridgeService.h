#pragma once

#include "Engine/Scene/MachiningDataBridge.h"

#include "Ut/Vec.h"

#include <cstdint>
#include <vector>

namespace Eg
{
    class SceneManager;
    class SceneManager3D;
}

/**
 * @brief 单层切片轮廓：一条世界 XY 折线 + 所属层高
 *
 * 这是 3D 切片结果跨模块传递的载体（POD 化，不含任何 Qt / 渲染类型）。
 */
struct MachiningLayerContour
{
    std::vector<Ut::Vec2d> points;
    double z{ 0.0 };
    bool closed{ true };
};

/**
 * @brief 切片提供者：由 Engraving 模块在可用时注册
 *
 * @param ctx         注册时传入的上下文
 * @param meshId      3D 网格图元 ID
 * @param layerHeight 层高
 * @param outLayers   输出：每层轮廓
 * @return 成功返回 true
 */
using MachiningSliceProvider = bool (*)(void* ctx,
    Eg::EntityId meshId,
    double layerHeight,
    std::vector<MachiningLayerContour>& outLayers);

/**
 * @brief 3D → 2D 加工数据桥实现（数据模型 C）
 *
 * - **注入侧（本类）**：把轮廓作为 2D 多边形图元写入 Engine2D 的 SceneManager，
 *   并记录产出 ID，供 queryMachiningResultIds 查询。
 * - **切片侧**：委托给 MachiningSliceProvider。Engraving 模块构建后注册真实实现
 *   （MeshSlicer）；未注册时 isAvailable() 返回 false，调用方据此提示而不是静默失败。
 *
 * 依赖方向：本类位于 Main（同时可见 Engine2D 与 Engine3D），
 * 接口与模型定义在 EngineCommon，二者都不反向依赖。
 */
class MachiningDataBridgeService final : public Eg::IMachiningDataBridge
{
public:
    /// @param scene2D 目标 2D 场景（注入目的地）
    /// @param scene3D 3D 场景（校验来源网格，可为 nullptr）
    MachiningDataBridgeService(Eg::SceneManager* scene2D, Eg::SceneManager3D* scene3D);
    ~MachiningDataBridgeService() override;

    /// 注册切片提供者（Engraving 模块可用时调用）
    void setSliceProvider(MachiningSliceProvider provider, void* ctx);

    // ---- Eg::IMachiningDataBridge ----

    bool isAvailable() const override;

    bool sliceAndInject(Eg::EntityId meshId,
                        double layerHeight,
                        Eg::ISceneContext* target2D,
                        uint32_t* outLayerCount) override;

    uint32_t queryMachiningResultIds(Eg::ISceneContext* ctx2D,
                                     Eg::EntityId* outIds,
                                     uint32_t capacity) override;

    /// 清空本桥产出的 2D 加工图元记录（不删除场景图元，仅重置追踪）
    void resetTrackedResults();

private:
    Eg::SceneManager* m_scene2D{ nullptr };
    Eg::SceneManager3D* m_scene3D{ nullptr };

    MachiningSliceProvider m_provider{ nullptr };
    void* m_providerCtx{ nullptr };

    /// 本桥注入的 2D 图元 ID（MachiningResult 角色的追踪集）
    std::vector<Eg::EntityId> m_injectedIds;
};
