#pragma once
/**
 * @file ISelectionService.h
 * @brief 选择服务窄接口
 *
 * 选择状态管理，与文档事实分离。
 */
#include <string>
#include <vector>

class ISelectionService
{
public:
    virtual ~ISelectionService() = default;

    virtual std::vector<std::string> selectedIds() const = 0;
    virtual bool isSelected(const std::string& id) const = 0;
    virtual void select(const std::string& id) = 0;
    virtual void selectMultiple(const std::vector<std::string>& ids) = 0;
    virtual void deselect(const std::string& id) = 0;
    virtual void clear() = 0;
    virtual void toggle(const std::string& id) = 0;
};
