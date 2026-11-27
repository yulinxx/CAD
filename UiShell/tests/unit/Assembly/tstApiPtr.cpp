#include <QtTest/QtTest>

#include <type_traits>

#include "UiShell/Assembly/IRegionContainer.h"
#include "UiShell/Assembly/IShellRuntime.h"
#include "UiShell/Contract/DialogRegistry.h"
#include "UiShell/Contract/FeatureFlag.h"
#include "UiShell/Contract/IApiObject.h"
#include "UiShell/Contract/IShellHost.h"
#include "UiShell/Contract/WidgetFactory.h"
#include "UiShell/Titlebar/ITitleBarView.h"
#include "UiShell/i18n/IQtTextProvider.h"
#include "UiShell/i18n/ITranslationService.h"

// 跨模块对象只能经 destroy() / ApiPtr 释放：析构函数必须不可公开访问（对接口指针写 delete 编译不过）。
static_assert(!std::is_destructible<RD::IApiObject>::value, "IApiObject must not be deletable");
static_assert(!std::is_destructible<RD::IShellRuntime>::value, "IShellRuntime must not be deletable");
static_assert(!std::is_destructible<RD::IDialogFactoryRegistry>::value, "IDialogFactoryRegistry must not be deletable");
static_assert(!std::is_destructible<RD::IWidgetFactoryRegistry>::value, "IWidgetFactoryRegistry must not be deletable");
static_assert(!std::is_destructible<RD::IFeatureFlagStore>::value, "IFeatureFlagStore must not be deletable");
static_assert(!std::is_destructible<RD::ITitleBarView>::value, "ITitleBarView must not be deletable");
static_assert(!std::is_destructible<RD::ITranslationService>::value, "ITranslationService must not be deletable");
static_assert(!std::is_destructible<RD::IQtTextProvider>::value, "IQtTextProvider must not be deletable");
// shell 持有、调用方只借用的接口：同样不可 delete。
static_assert(!std::is_destructible<RD::IShellHost>::value, "IShellHost is borrowed, not owned");
static_assert(!std::is_destructible<RD::IRegionContainer>::value, "IRegionContainer is borrowed, not owned");
static_assert(!std::is_destructible<RD::IFeatureFlagObserver>::value, "observer is not owned by the store");
static_assert(!std::is_destructible<RD::IShellListener>::value, "listener is not owned by the shell");

using namespace RD;

namespace
{
    struct TrackedObject final : IApiObject
    {
        explicit TrackedObject(int* counter)
            : destroys(counter)
        {
        }

        void destroy() override
        {
            ++*destroys;
            delete this;
        }

        int* destroys = nullptr;
    };
} // namespace

class ApiPtrTest : public QObject
{
    Q_OBJECT

private slots:
    void selfResetIsNoOp();
    void resetReplacesAndDestroysOldObject();
    void destructorReleasesOnce();
    void moveTransfersOwnership();
};

void ApiPtrTest::selfResetIsNoOp()
{
    int destroys = 0;
    auto* object = new TrackedObject(&destroys);

    ApiPtr<IApiObject> pointer(object);
    pointer.reset(object);

    // 自重置不得销毁对象，也不得改变持有指针。
    QCOMPARE(pointer.get(), static_cast<IApiObject*>(object));
    QCOMPARE(destroys, 0);

    pointer.reset();
    QCOMPARE(destroys, 1);
    QVERIFY(!pointer);
}

void ApiPtrTest::resetReplacesAndDestroysOldObject()
{
    int destroys = 0;
    auto* first = new TrackedObject(&destroys);
    auto* second = new TrackedObject(&destroys);

    ApiPtr<IApiObject> pointer(first);
    pointer.reset(second);

    QCOMPARE(destroys, 1);
    QCOMPARE(pointer.get(), static_cast<IApiObject*>(second));

    pointer.reset();
    QCOMPARE(destroys, 2);
}

void ApiPtrTest::destructorReleasesOnce()
{
    int destroys = 0;
    {
        ApiPtr<IApiObject> pointer(new TrackedObject(&destroys));
        QCOMPARE(destroys, 0);
    }
    QCOMPARE(destroys, 1);
}

void ApiPtrTest::moveTransfersOwnership()
{
    int destroys = 0;
    auto* object = new TrackedObject(&destroys);

    ApiPtr<IApiObject> source(object);
    ApiPtr<IApiObject> target(std::move(source));

    QVERIFY(!source);
    QCOMPARE(target.get(), static_cast<IApiObject*>(object));
    QCOMPARE(destroys, 0);

    target.reset();
    QCOMPARE(destroys, 1);
}

QTEST_GUILESS_MAIN(ApiPtrTest)

#include "tstApiPtr.moc"