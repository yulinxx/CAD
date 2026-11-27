#pragma once

#include <QHash>

#include "UiShell/Contract/WidgetFactory.h"

namespace RD
{
    // 简单工厂注册表
    class WidgetFactoryRegistry final : public IWidgetFactoryRegistry
    {
    public:
        // 登记工厂 id 到工厂实例
        void registerFactory(const QString& factoryId, IWidgetFactory* factory) override;

        // 按工厂 id 查找工厂
        IWidgetFactory* find(const QString& factoryId) const override;

        // 释放动作放在 shell 内编译，避免跨 CRT 直接 delete。
        void destroy() override;

    private:
        QHash<QString, IWidgetFactory*> m_factories;
    };
}  // namespace RD
