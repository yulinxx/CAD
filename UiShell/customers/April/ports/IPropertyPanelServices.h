#pragma once

#include <QStringList>
#include <memory>

#include "UI/Dlg/PropertyModel.h"

class IPropertyEditTarget;

namespace Apr
{
    // 属性面板窄接口：面板只消费 PropertyModel / IPropertyEditTarget，
    // 不接触 SceneManager、SceneEditService 或撤销实现。由 AprilCore 适配层实现。
    class IPropertyPanelServices
    {
    public:
        virtual ~IPropertyPanelServices() = default;

        // ids 来自场景树选中项（字符串）；算法层负责解析并生成展示模型。
        virtual PropertyModel buildPropertyModel(const QStringList& ids) const = 0;

        // 返回可提交编辑的目标；不可编辑时返回空指针。
        virtual std::shared_ptr<IPropertyEditTarget> createEditTarget(const QStringList& ids) = 0;
    };
}  // namespace Apr
