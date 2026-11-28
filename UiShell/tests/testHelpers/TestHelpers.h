#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>

#include "UiShell/Export.h"
#include "UiShell/Assembly/IShellRuntime.h"
#include "UiShell/Assembly/ShellServices.h"
#include "UiShell/Contract/Command.h"
#include "UiShell/Contract/WidgetFactory.h"
#include "UiShell/i18n/ITranslationService.h"
#include "UiShell/Titlebar/ITitleBarHost.h"

class QObject;

// UiTestHelpers 始终是静态库，直接链接进各测试可执行文件：
// 仅在构建该库（UI_TESTHELPERS_EXPORTS）时导出；消费端一律为空，直接引用静态库中的真实符号。
// 注意：消费端不能走 dllimport——静态库不会生成 __imp_ 桩，否则会 LNK2019。
#if defined(UI_TESTHELPERS_EXPORTS)
#  define UI_TEST_API UI_DECL_EXPORT
#else
#  define UI_TEST_API
#endif

namespace AA::test
{
    // 记录被转发的命令 id。
    class UI_TEST_API FakeCommandInvoker : public ICommandInvoker
    {
    public:
        void invoke(const QString& commandId) override;

        QStringList invoked;
    };

    // 只创建一个带固定对象名的 QLabel，并记录创建次数。
    class UI_TEST_API FakeWidgetFactory : public IWidgetFactory
    {
    public:
        QWidget* create(const PanelCreateRequest& info) override;

        int created = 0;
    };

    // 只保存指针、不拥有工厂：与框架注册表的语义一致。
    class UI_TEST_API FakeWidgetFactoryRegistry : public IWidgetFactoryRegistry
    {
    public:
        void registerFactory(const QString& factoryId, IWidgetFactory* factory) override;
        IWidgetFactory* find(const QString& factoryId) const override;

        void destroy() override
        {
            delete this;
        }

    private:
        QHash<QString, IWidgetFactory*> m_factories;
    };

    class UI_TEST_API FakeCenterViewProvider : public ICenterViewProvider
    {
    public:
        QWidget* createCenterView(QWidget* parent) override;
    };

    // 记录标题栏请求次数的窄宿主替身；isMaximized 可随时改写。
    class UI_TEST_API FakeTitleBarHost : public ITitleBarHost
    {
    public:
        void requestDrag() override;
        void requestMinimize() override;
        void requestToggleMaximize() override;
        void requestClose() override;
        bool isMaximized() const override;

        int dragCount = 0;
        int minimizeCount = 0;
        int toggleMaximizeCount = 0;
        int closeCount = 0;
        bool maximized = false;
    };

    // 按传入的 regions/titleBar/menus/panels 拼出完整 profile JSON。
    UI_TEST_API QByteArray makeProfileJson(const QString& profileId,
        const QJsonArray& regions,
        const QJsonObject& titleBar = QJsonObject(),
        const QJsonArray& menus = QJsonArray(),
        const QJsonArray& panels = QJsonArray());

    // 最小可用 profile：只有一个 CenterView 区域。
    UI_TEST_API QByteArray minimalProfileJson(const QString& profileId = QStringLiteral("test"));

    // 按 profile JSON 与依赖构造运行期；失败原因由调用方通过 lastError() 检查。
    UI_TEST_API ApiPtr<IShellRuntime> buildRuntime(const QByteArray& profileJson,
        const ShellServices& services = ShellServices());

    // RAII 安装/卸载翻译，避免用例之间残留全局 translator。
    class UI_TEST_API TranslationGuard
    {
    public:
        explicit TranslationGuard(const QString& language, const QString& directory = QString());
        ~TranslationGuard();

        TranslationGuard(const TranslationGuard&) = delete;
        TranslationGuard& operator=(const TranslationGuard&) = delete;

        bool isInstalled() const;
        QString lastError() const;

    private:
        ApiPtr<ITranslationService> m_service;
    };

    template <typename T>
    T* findByObjectName(QObject* root, const char* name)
    {
        return root ? root->findChild<T*>(QString::fromLatin1(name)) : nullptr;
    }
} // namespace AA::test
