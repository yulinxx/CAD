#pragma once

#include <QSet>
#include <QObject>
#include <QString>

#include "UiShell/Export.h"
#include "UiShell/Contract/IApiObject.h"

namespace RD
{
    // 功能开关集合
    class FeatureFlagSet
    {
    public:
        FeatureFlagSet() = default;

        explicit FeatureFlagSet(const QSet<QString>& enabled)
            : m_enabled(enabled)
        {
        }

    public:
        bool has(const QString& key) const
        {
            return m_enabled.contains(key);
        }

        // 启用开关
        bool enable(const QString& key)
        {
            if (m_enabled.contains(key))
                return false;

            m_enabled.insert(key);
            return true;
        }

        // 禁用
        bool disable(const QString& key)
        {
            if (!m_enabled.contains(key))
                return false;

            m_enabled.remove(key);
            return true;
        }

    private:
        QSet<QString> m_enabled;
    };

    // 功能开关变化的观察者
    class IFeatureFlagObserver
    {
    public:
        virtual void onFeatureFlagsChanged() = 0;

    protected:
        virtual ~IFeatureFlagObserver() = default;
    };

    // 功能开关仓库
    class IFeatureFlagStore : public IApiObject
    {
    protected:
        // 使用 destroy() 释放，禁止跨模块直接 delete
        ~IFeatureFlagStore() override = default;

    public:
        virtual FeatureFlagSet snapshot() const = 0;

        // 版本号递增，调用方可判断快照是否需要重取
        virtual quint64 revision() const = 0;

        // 订阅变化通知；返回订阅令牌（非 0），observer 为空时返回 0
        virtual quint64 subscribe(IFeatureFlagObserver* observer) = 0;

        // 取消指定令牌的订阅；无效或已退订时静默忽略
        virtual void unsubscribe(quint64 subscriptionId) = 0;

        // 启用/禁用开关；状态变更时产生新快照并触发订阅者回调
        virtual void enable(const QString& key) = 0;

        virtual void disable(const QString& key) = 0;
    };

    // 创建空的功能开关 store；订阅回调派发到创建线程，应在 GUI 线程创建。
    UI_SHELL_API IFeatureFlagStore* createFeatureFlagStore();
}  // namespace RD
