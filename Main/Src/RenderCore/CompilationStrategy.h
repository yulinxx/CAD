#pragma once

#include "RenderContext.h"

#include <set>
#include <string>

/**
 * @file CompilationStrategy.h
 * @brief 编译策略决策器
 *
 * 负责决定使用增量编译还是全量编译，管理脏实体追踪。
 *
 * 职责边界：
 * - 判断是否可以进行增量编译
 * - 管理脏实体 ID 集合
 * - 控制强制全量编译标志
 * - 提供脏标记 API
 *
 * 不承担：
 * - 实际编译执行（由 SceneTraverser 负责）
 * - 缓存管理（由 BatchManager 负责）
 * - 批次生成（由 SceneTraverser 负责）
 */
class CompilationStrategy
{
public:
    /// 判断是否可以进行增量编译
    bool canIncrementalCompile(const RenderContext& context) const;

    /// 获取当前脏实体 ID 集合
    const std::set<std::string>& dirtyEntityIds() const;

    /// 标记单个实体为脏
    void markEntityDirty(const std::string& entityId);

    /// 标记所有实体为脏（强制全量编译）
    void markAllDirty();

    /// 清除脏标记
    void clearDirty();

    /// 检查是否有脏实体
    bool hasDirtyEntities() const;

    /// 设置强制全量编译
    void setForceFullCompile(bool force);

    /// 获取强制全量编译标志
    bool forceFullCompile() const;

    /// 设置缓存有效状态
    void setCacheValid(bool valid);

    /// 获取缓存有效状态
    bool cacheValid() const;

private:
    bool m_cacheValid{ false };
    bool m_forceFullCompile{ false };
    std::set<std::string> m_dirtyEntityIds;
};
