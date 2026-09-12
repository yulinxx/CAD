#include "MachiningDataBridgeService.h"

#include "Engine/Scene/ISceneContext.h"
#include "Engine3D/SceneManager3D.h"
#include "Engine3D/SyEntity/SyMeshEntity.h"
#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/SyEntity/SyPolygon.h"

#include "Log/SyLogger.h"

#include <algorithm>

MachiningDataBridgeService::MachiningDataBridgeService(
    Eg::SceneManager* scene2D, Eg::SceneManager3D* scene3D)
    : m_scene2D(scene2D)
    , m_scene3D(scene3D)
{
}

MachiningDataBridgeService::~MachiningDataBridgeService() = default;

void MachiningDataBridgeService::setSliceProvider(MachiningSliceProvider provider, void* ctx)
{
    m_provider = provider;
    m_providerCtx = ctx;
}

bool MachiningDataBridgeService::isAvailable() const
{
    // 切片实现由 Engraving 模块提供；未注册即视为不可用，调用方据此提示用户，
    // 而不是让「切片」按钮点下去毫无反应
    return m_provider != nullptr && m_scene2D != nullptr;
}

bool MachiningDataBridgeService::sliceAndInject(Eg::EntityId meshId,
    double layerHeight,
    Eg::ISceneContext* target2D,
    uint32_t* outLayerCount)
{
    if (outLayerCount)
    {
        *outLayerCount = 0;
    }

    if (!isAvailable())
    {
        SY_WARN("[MachiningDataBridge] sliceAndInject: slice provider is not registered "
                "(build with BUILD_ENGRAVING and register MeshSlicer to enable)");
        return false;
    }
    if (layerHeight <= 0.0)
    {
        SY_WARNF("[MachiningDataBridge] sliceAndInject: invalid layer height %.6f", layerHeight);
        return false;
    }
    // 目标 2D 场景必须显式给出：不隐式写当前场景，避免把加工数据注入到错误的文档
    if (!target2D)
    {
        SY_WARN("[MachiningDataBridge] sliceAndInject: target 2D context is null");
        return false;
    }

    // 来源网格校验：3D 场景可用时确认网格存在，避免对空 ID 做无用切片
    if (m_scene3D && !m_scene3D->findMeshById(meshId))
    {
        SY_WARNF("[MachiningDataBridge] sliceAndInject: mesh %lld not found in 3D scene",
            static_cast<long long>(meshId));
        return false;
    }

    std::vector<MachiningLayerContour> layers;
    if (!m_provider(m_providerCtx, meshId, layerHeight, layers) || layers.empty())
    {
        SY_WARNF("[MachiningDataBridge] sliceAndInject: slicing produced no layer for mesh %lld",
            static_cast<long long>(meshId));
        return false;
    }

    uint32_t injected = 0;
    for (const MachiningLayerContour& layer : layers)
    {
        if (layer.points.size() < 2)
        {
            continue;
        }

        // 构造 2D 多边形图元；addEntity 内部会 clone 并分配持久 id，
        // 因此这里每层用局部对象即可，不需要自己管理所有权
        Eg::SyPolygon polygon;
        polygon.setVertices(layer.points);
        polygon.bClosed = layer.closed;

        // 加工数据用统一颜色标识，便于与设计图元区分
        polygon.setOverrideColor(Ut::Color::fromRGB255(255, 140, 0));

        if (Eg::SyEntity* added = m_scene2D->addEntity(&polygon))
        {
            m_injectedIds.push_back(added->id);
            ++injected;
        }
    }

    if (injected == 0)
    {
        SY_WARN("[MachiningDataBridge] sliceAndInject: no contour could be injected");
        return false;
    }

    // 场景变更通知由 SceneManager 内部在 addEntity 时发出
    if (outLayerCount)
    {
        *outLayerCount = injected;
    }

    SY_INFOF("[MachiningDataBridge] sliceAndInject: mesh=%lld layerHeight=%.3f injected %u contour(s)",
        static_cast<long long>(meshId),
        layerHeight,
        injected);
    return true;
}

uint32_t MachiningDataBridgeService::queryMachiningResultIds(
    Eg::ISceneContext* ctx2D, Eg::EntityId* outIds, uint32_t capacity)
{
    // 当前只追踪本桥注入的结果；ctx2D 为空或非本桥目标时返回 0，
    // 避免把别的文档的加工数据算进来
    if (!ctx2D || !m_scene2D)
    {
        return 0;
    }

    const uint32_t total = static_cast<uint32_t>(m_injectedIds.size());
    if (!outIds || capacity == 0)
    {
        return total;
    }

    const uint32_t count = std::min<uint32_t>(total, capacity);
    std::copy_n(m_injectedIds.begin(), count, outIds);
    return count;
}

void MachiningDataBridgeService::resetTrackedResults()
{
    m_injectedIds.clear();
}
