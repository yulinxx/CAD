#pragma once
/**
 * @file IDocumentSession.h
 * @brief 文档会话窄接口 — POD 安全跨 DLL 设计
 *
 * 命令通过此接口与文档交互，而不是直接操作 SceneManager。
 *
 * @section ABI 安全设计 (P2 收口)
 * - 所有虚函数参数/返回值使用 POD 类型（const char*, void*, 原始函数指针）
 * - 禁止在虚函数中使用 std::string、std::vector
 * - 集合遍历使用回调模式（visitor + context）
 *
 * @note 此接口当前无实现类，实际文档会话参见 SceneDocument2D
 */
#include <cstddef>

namespace Eg
{
    struct SyEntity;
}

/// 实体 ID 遍历回调：id 为 null-terminated C string，context 为调用方透传上下文
typedef void (*DocEntityIdVisitor)(const char* id, void* context);

class IDocumentSession
{
public:
    virtual ~IDocumentSession() = default;

    /// 按 ID 查找图元（返回不透明指针，调用方自行转换）
    virtual Eg::SyEntity* findEntity(const char* id) const = 0;

    /// 遍历所有选中项的 ID（回调模式，POD 安全）
    virtual void visitSelectedIds(DocEntityIdVisitor visitor, void* context) const = 0;

    /// 选中指定图元（清除之前的选择）
    virtual void selectEntity(const char* id) = 0;

    /// 清空所有选择
    virtual void clearSelection() = 0;

    /// 删除指定图元
    virtual void removeEntity(const char* id) = 0;
};