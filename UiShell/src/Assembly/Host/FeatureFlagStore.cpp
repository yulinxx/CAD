#include "FeatureFlagStore.h"

#include <QMutexLocker>
#include <QThread>

namespace AA
{
    FeatureFlagStore::FeatureFlagStore(QObject* parent)
        : QObject(parent)
        , m_revision(0)
    {
        // 须在 GUI 线程执行，避免跨线程操作控件
        connect(this, &FeatureFlagStore::sigChanged, this, &FeatureFlagStore::slotDispatch, Qt::QueuedConnection);
    }

    FeatureFlagSet FeatureFlagStore::snapshot() const
    {
        QMutexLocker locker(&m_mutex);
        return m_flags;
    }

    quint64 FeatureFlagStore::revision() const
    {
        QMutexLocker locker(&m_mutex);
        return m_revision;
    }

    quint64 FeatureFlagStore::subscribe(IFeatureFlagObserver* observer)
    {
        if (!observer)
            return 0;

        QMutexLocker locker(&m_mutex);
        const quint64 id = m_nextSubscriptionId++;
        m_subscriptions.insert(id, observer);
        return id;
    }

    void FeatureFlagStore::unsubscribe(quint64 subscriptionId)
    {
        QMutexLocker locker(&m_mutex);
        m_subscriptions.remove(subscriptionId);
    }

    void FeatureFlagStore::enable(const QString& key)
    {
        bool didChange = false;
        {
            QMutexLocker locker(&m_mutex);
            if (m_flags.enable(key))
            {
                ++m_revision;
                didChange = true;
            }
        }

        if (didChange)
            emit sigChanged();
    }

    void FeatureFlagStore::disable(const QString& key)
    {
        bool didChange = false;
        {
            QMutexLocker locker(&m_mutex);
            if (m_flags.disable(key))
            {
                ++m_revision;
                didChange = true;
            }
        }

        if (didChange)
            emit sigChanged();
    }

    void FeatureFlagStore::slotDispatch()
    {
        if (QThread::currentThread() != thread())
        {
            QMetaObject::invokeMethod(this, &FeatureFlagStore::slotDispatch, Qt::QueuedConnection);
            return;
        }

        QList<quint64> ids;
        {
            QMutexLocker locker(&m_mutex);
            ids = m_subscriptions.keys();
        }

        for (const quint64 id : ids)
        {
            IFeatureFlagObserver* observer = nullptr;
            {
                QMutexLocker locker(&m_mutex);
                observer = m_subscriptions.value(id, nullptr);
            }
            if (observer)
                observer->onFeatureFlagsChanged();
        }
    }

    IFeatureFlagStore* createFeatureFlagStore()
    {
        return new FeatureFlagStore();
    }
}  // namespace AA