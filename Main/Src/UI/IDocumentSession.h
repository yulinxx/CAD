#pragma once
/**
 * @file IDocumentSession.h
 * @brief 文档会话窄接口
 *
 * 命令通过此接口与文档交互，而不是直接操作 SceneManager。
 */
#include <string>
#include <vector>

namespace Eg
{
    struct SyEntity;
}

class IDocumentSession
{
public:
    virtual ~IDocumentSession() = default;

    virtual Eg::SyEntity* findEntity(const std::string& id) const = 0;
    virtual std::vector<std::string> selectedIds() const = 0;
    virtual void selectEntity(const std::string& id) = 0;
    virtual void clearSelection() = 0;
    virtual void removeEntity(const std::string& id) = 0;
};
