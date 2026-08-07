#pragma once
/**
 * @file ISelectionService.h
 * @brief 选择服务窄接口 — POD 安全跨 DLL 设计 (仅 2D)
 *
 * 选择状态管理，与文档事实分离。
 *
 * @section 适用范围
 * 本接口仅用于 2D 场景的选择管理，实现类 SelectionService 包装 Eg::SceneManager。
 * 3D 场景的选择管理走独立的 Eg::SelectionManager3D，不实现本接口。
 * 这是因为 2D/3D 选择语义差异显著（射线拾取、框选、变换 gizmo 等），
 * 强行统一会导致接口膨胀且增加耦合，当前阶段保持独立更利于维护。
 *
 * @section ABI 安全设计 (P2 收口)
 * - 所有虚函数参数/返回值使用 POD 类型（const char*, size_t, 原始函数指针）
 * - 禁止在虚函数中使用 std::string、std::vector、std::function（跨 DLL 崩溃风险）
 * - 集合遍历使用回调模式（visitor + context），避免跨 DLL 内存分配/释放
 * - 内部实现可自由使用 STL，但不得暴露到虚函数签名中
 *
 * @section 实体访问说明 (v1.13 收口)
 * 本接口仅暴露 const char* ID，不泄漏 Eg::SyEntity*。
 * 调用方需要直接读取图元几何数据时，应通过 visitSelectedIds 获取 ID，
 * 再用 Eg::SceneManager::findEntityById(id) 查询实体指针。
 * 这样 ISelectionService 成为纯 ID 接口，彻底消除引擎类型泄漏。
 */

#include <cstddef>

// ==================== POD 安全回调类型 ====================

/// 选中 ID 遍历回调：id 为 null-terminated C string，context 为调用方透传上下文
typedef void (*SelectedIdVisitor)(const char* id, void* context);

// ==================== 选择服务接口 (2D 专用) ====================

class ISelectionService
{
public:
    virtual ~ISelectionService() = default;

    // ---- POD 安全：遍历选中 ID（替代 std::vector<std::string> selectedIds()）----
    /// 对每个选中 ID 调用 visitor，context 透传给回调
    virtual void visitSelectedIds(SelectedIdVisitor visitor, void* context) const = 0;

    // ---- POD 安全：const char* 替代 std::string ----
    /// 检查指定 ID 是否被选中
    virtual bool isSelected(const char* id) const = 0;

    /// 选中单个项（清除之前的选择）
    virtual void select(const char* id) = 0;

    /// 批量选中（ids 为 C string 数组，count 为数量）
    virtual void selectMultiple(const char* const* ids, size_t count) = 0;

    /// 取消选中指定项
    virtual void deselect(const char* id) = 0;

    /// 清空所有选择
    virtual void clear() = 0;

    /// 切换指定项的选中状态
    virtual void toggle(const char* id) = 0;
};
