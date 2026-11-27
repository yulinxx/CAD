#include <QtTest/QtTest>

#include <QThread>

#include <functional>
#include <thread>
#include <utility>

#include "Assembly/Host/FeatureFlagStore.h"

using namespace RD;

namespace
{
    // 测试用观察者：把回调转给本模块内的 std::function（std::function 只在测试内部使用，不跨 DLL）。
    class CallbackObserver final : public IFeatureFlagObserver
    {
    public:
        explicit CallbackObserver(std::function<void()> callback)
            : m_callback(std::move(callback))
        {
        }

        void onFeatureFlagsChanged() override
        {
            m_callback();
        }

    private:
        std::function<void()> m_callback;
    };
}  // namespace

class FeatureFlagStoreTest : public QObject
{
    Q_OBJECT

private slots:
    void enableNotifiesSubscriberOnOwningThread();
    void duplicateEnableDoesNotNotify();
    void disableNotifiesSubscriber();
    void unsubscribeStopsNotifications();
    void nullObserverIsRejected();
    void duplicateSubscriptionsAreIndependent();
    void callbackCanUnsubscribeItself();
    void observerUnsubscribedDuringDispatchIsSkipped();
    void backgroundThreadEnableDispatchesOnOwningThread();
};

void FeatureFlagStoreTest::enableNotifiesSubscriberOnOwningThread()
{
    ApiPtr<IFeatureFlagStore> store(createFeatureFlagStore());
    QVERIFY(store);

    QThread* mainThread = QThread::currentThread();
    int calls = 0;
    QThread* callbackThread = nullptr;
    CallbackObserver observer([&]() {
        ++calls;
        callbackThread = QThread::currentThread();
    });
    store->subscribe(&observer);

    store->enable(QStringLiteral("feature.a"));

    // 回调经队列连接投递，需要事件循环处理后才执行。
    QTRY_COMPARE(calls, 1);
    QCOMPARE(callbackThread, mainThread);
}

void FeatureFlagStoreTest::duplicateEnableDoesNotNotify()
{
    ApiPtr<IFeatureFlagStore> store(createFeatureFlagStore());
    QVERIFY(store);

    int calls = 0;
    CallbackObserver observer([&]() { ++calls; });
    store->subscribe(&observer);

    store->enable(QStringLiteral("feature.a"));
    QTRY_COMPARE(calls, 1);

    // 重复启用同一开关不应产生新快照，也不应触发回调。
    store->enable(QStringLiteral("feature.a"));
    QTest::qWait(50);
    QCOMPARE(calls, 1);
    QCOMPARE(store->revision(), quint64(1));
}

void FeatureFlagStoreTest::disableNotifiesSubscriber()
{
    ApiPtr<IFeatureFlagStore> store(createFeatureFlagStore());
    QVERIFY(store);

    int calls = 0;
    CallbackObserver observer([&]() { ++calls; });
    store->subscribe(&observer);

    store->enable(QStringLiteral("feature.a"));
    QTRY_COMPARE(calls, 1);
    QVERIFY(store->snapshot().has(QStringLiteral("feature.a")));

    store->disable(QStringLiteral("feature.a"));

    QTRY_COMPARE(calls, 2);
    QVERIFY(!store->snapshot().has(QStringLiteral("feature.a")));
    QCOMPARE(store->revision(), quint64(2));

    // 重复禁用不存在的开关不应触发回调。
    store->disable(QStringLiteral("feature.a"));
    QTest::qWait(50);
    QCOMPARE(calls, 2);
}

void FeatureFlagStoreTest::unsubscribeStopsNotifications()
{
    ApiPtr<IFeatureFlagStore> store(createFeatureFlagStore());
    QVERIFY(store);

    int calls = 0;
    CallbackObserver observer([&]() { ++calls; });
    const quint64 token = store->subscribe(&observer);

    store->enable(QStringLiteral("feature.a"));
    QTRY_COMPARE(calls, 1);

    store->unsubscribe(token);
    store->enable(QStringLiteral("feature.b"));
    QTest::qWait(50);
    QCOMPARE(calls, 1);

    // 退订不存在的令牌应静默忽略。
    store->unsubscribe(token);
    store->unsubscribe(0);
}

void FeatureFlagStoreTest::nullObserverIsRejected()
{
    ApiPtr<IFeatureFlagStore> store(createFeatureFlagStore());
    QVERIFY(store);
    QCOMPARE(store->subscribe(nullptr), quint64(0));

    // 没有有效订阅时开关变化不应出错。
    store->enable(QStringLiteral("feature.a"));
    QTest::qWait(50);
}

void FeatureFlagStoreTest::duplicateSubscriptionsAreIndependent()
{
    ApiPtr<IFeatureFlagStore> store(createFeatureFlagStore());
    QVERIFY(store);

    int calls = 0;
    CallbackObserver observer([&]() { ++calls; });
    const quint64 first = store->subscribe(&observer);
    const quint64 second = store->subscribe(&observer);
    QVERIFY(first != second);

    store->enable(QStringLiteral("feature.a"));
    QTRY_COMPARE(calls, 2);

    // 只退订其中一个令牌，另一个订阅仍然生效。
    store->unsubscribe(first);
    store->enable(QStringLiteral("feature.b"));
    QTRY_COMPARE(calls, 3);

    store->unsubscribe(second);
    store->enable(QStringLiteral("feature.c"));
    QTest::qWait(50);
    QCOMPARE(calls, 3);
}

void FeatureFlagStoreTest::callbackCanUnsubscribeItself()
{
    ApiPtr<IFeatureFlagStore> store(createFeatureFlagStore());
    QVERIFY(store);

    int calls = 0;
    quint64 token = 0;
    CallbackObserver observer([&]() {
        ++calls;
        store->unsubscribe(token);
    });
    token = store->subscribe(&observer);
    QVERIFY(token != 0);

    store->enable(QStringLiteral("feature.a"));
    QTRY_COMPARE(calls, 1);

    // 回调内部退订后不应再收到后续变化。
    store->enable(QStringLiteral("feature.b"));
    QTest::qWait(50);
    QCOMPARE(calls, 1);
}

// 观察者是裸指针：某个回调里退订（之后可能销毁）了别的观察者，本轮派发不得再调用它。
void FeatureFlagStoreTest::observerUnsubscribedDuringDispatchIsSkipped()
{
    ApiPtr<IFeatureFlagStore> store(createFeatureFlagStore());
    QVERIFY(store);

    int firstCalls = 0;
    int secondCalls = 0;
    quint64 firstToken = 0;
    quint64 secondToken = 0;
    CallbackObserver first([&]() {
        ++firstCalls;
        store->unsubscribe(secondToken);
    });
    CallbackObserver second([&]() {
        ++secondCalls;
        store->unsubscribe(firstToken);
    });
    firstToken = store->subscribe(&first);
    secondToken = store->subscribe(&second);

    store->enable(QStringLiteral("feature.a"));

    // 派发顺序不保证：先被调用的那个退订另一个，因此本轮恰好只有一个被调用。
    QTRY_COMPARE(firstCalls + secondCalls, 1);
    QTest::qWait(50);
    QCOMPARE(firstCalls + secondCalls, 1);
}

void FeatureFlagStoreTest::backgroundThreadEnableDispatchesOnOwningThread()
{
    ApiPtr<IFeatureFlagStore> store(createFeatureFlagStore());
    QVERIFY(store);

    QThread* mainThread = QThread::currentThread();
    QThread* callbackThread = nullptr;
    int calls = 0;
    CallbackObserver observer([&]() {
        ++calls;
        callbackThread = QThread::currentThread();
    });
    store->subscribe(&observer);

    std::thread worker([&]() { store->enable(QStringLiteral("feature.a")); });
    worker.join();

    // 即使 enable 来自后台线程，订阅回调也必须在 store 所属线程执行。
    QTRY_COMPARE(calls, 1);
    QCOMPARE(callbackThread, mainThread);
    QVERIFY(store->snapshot().has(QStringLiteral("feature.a")));
}

QTEST_GUILESS_MAIN(FeatureFlagStoreTest)

#include "tstFeatureFlagStore.moc"
