#pragma once

#include <memory>
#include <string>
#include <vector>

#include "RenderCoreApi.h"
#include "RenderContext.h"
#include "RenderFrame.h"
#include "RenderTypes.h"

class SceneDocument3D;

namespace Eg { class SceneManager; }

/**
 * @file SceneCompiler.h
 * @brief 场景编译器抽象接口
 *
 * 把 2D / 3D 文档编译成统一的渲染帧数据。
 *
 * 核心职责：
 * - 遍历场景实体
 * - 生成绘制批次（RenderBatch）
 * - 按类型分组合并批次
 * - 做视锥裁剪
 * - 做脏区增量更新
 * - 缓存不变实体
 *
 * 不应承担：
 * - 具体图形 API 调用
 * - GPU 资源管理
 * - 帧缓冲操作
 */
class RENDER_CORE_API SceneCompiler
{
public:
    virtual ~SceneCompiler() = default;

    // ============ 全量编译 ============

    /// 全量编译 2D 场景
    virtual RenderFrame compile(Eg::SceneManager* scene, const RenderContext& context) = 0;

    /// 全量编译 3D 场景
    virtual RenderFrame compile(SceneDocument3D* document, const RenderContext& context) = 0;

    // ============ 增量编译 ============

    /// 增量编译 2D 场景
    virtual RenderFrame compileIncremental(Eg::SceneManager* scene,
                                           const RenderContext& context,
                                           const RenderFrame& previousFrame) = 0;

    /// 增量编译 3D 场景
    virtual RenderFrame compileIncremental(SceneDocument3D* document,
                                           const RenderContext& context,
                                           const RenderFrame& previousFrame) = 0;

    // ============ 缓存控制 ============

    /// 清除编译缓存（场景结构变化时调用）
    virtual void invalidateCache() = 0;

    /// 是否持有有效缓存
    virtual bool hasCachedFrame() const = 0;

    /// 获取缓存帧号
    virtual uint64_t cachedFrameId() const = 0;

    // ============ 脏实体追踪（增量编译） ============

    /// 标记指定实体为脏（下次编译时仅重编译该实体及其关联批次）
    /// @param entityId 实体 ID
    virtual void markEntityDirty(const std::string& entityId) = 0;

    /// 标记所有实体为脏（强制全量编译）
    virtual void markAllDirty() = 0;

    // ============ 批次查询 ============

    /// 将渲染帧中的批次按图元类型分组（用于批量渲染优化）
    /// @return 按 PrimitiveType 分组的批次索引列表
    virtual std::vector<int> groupBatchesByPrimitiveType(const RenderFrame& frame) const = 0;

    /// 对视口外的批次进行裁剪
    /// @param frame 待裁剪的帧
    /// @param viewportRect 视口矩形（世界坐标）
    /// @return 裁剪后的帧
    virtual RenderFrame cullBatches(const RenderFrame& frame, const RenderRectF& viewportRect) const = 0;
};
