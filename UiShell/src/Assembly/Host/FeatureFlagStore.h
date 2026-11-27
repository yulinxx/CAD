#pragma once

#include "UiShell/Contract/FeatureFlag.h"
#include <QObject>
#include <QHash>
#include <QMutex>

namespace RD
{
    // 功能开关仓库
    class FeatureFlagStore : public QObject, public IFeatureFlagStore
    {
        Q_OBJECT

    public:
        explicit FeatureFlagStore(QObject* parent = nullptr);

    public:
        FeatureFlagSet snapshot() const override;
        quint64 revision() const override;
        quint64 subscribe(IFeatureFlagObserver* observer) override;
        void unsubscribe(quint64 subscriptionId) override;

        // 业务侧：启用/禁用一项开关。仅在状态实际变更时产生新快照并通知订阅者。
        void enable(const QString& key) override;
        void disable(const QString& key) override;

        void destroy() override
        {
            delete this;
        }

    Q_SIGNALS:
        // 开关集合发生变化；订阅者应自行调用 snapshot() 取新值。
        void sigChanged();

    private Q_SLOTS:
        void slotDispatch();

    private:
        mutable QMutex m_mutex;
        FeatureFlagSet m_flags;      // 当前启用的开关集合
        quint64 m_revision;
        quint64 m_nextSubscriptionId = 1;
        QHash<quint64, IFeatureFlagObserver*> m_subscriptions;  // 不持有观察者
    };
}  // namespace RD
