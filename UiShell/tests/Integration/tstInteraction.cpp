#include <QtTest/QtTest>

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QPointer>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QWidget>

#include "TestHelpers.h"
#include "UiShell/Assembly/IRegionContainer.h"
#include "UiShell/Assembly/IShellRuntime.h"
#include "UiShell/Assembly/ShellServices.h"
#include "UiShell/Contract/BuildFingerprint.h"
#include "UiShell/Contract/Command.h"
#include "UiShell/Contract/CommandTable.h"
#include "UiShell/Contract/CustomerModule.h"
#include "UiShell/Titlebar/ITitleBarView.h"
#include "UiShell/i18n/IQtTextProvider.h"
#include "UiShell/i18n/ITranslationService.h"
#include "UiDialogs/AboutDialogs.h"

using namespace RD;

namespace
{
    // 菜单文字使用稳定 key；未注入 text provider 时原样显示，命令项文字回退为命令 id。
    QByteArray menuProfileJson()
    {
        QJsonArray regions{
            QJsonObject{ { QStringLiteral("id"), QStringLiteral("menubar") },
                { QStringLiteral("type"), QStringLiteral("MenuBar") },
                { QStringLiteral("order"), 5 } },
            QJsonObject{ { QStringLiteral("id"), QStringLiteral("center") },
                { QStringLiteral("type"), QStringLiteral("CenterView") },
                { QStringLiteral("order"), 30 } }
        };
        QJsonArray menus{
            QJsonObject{ { QStringLiteral("type"), QStringLiteral("submenu") },
                { QStringLiteral("id"), QStringLiteral("file") },
                { QStringLiteral("titleKey"), QStringLiteral("File") },
                { QStringLiteral("order"), 10 },
                { QStringLiteral("children"), QJsonArray{ QStringLiteral("file.new") } } }
        };
        return test::makeProfileJson(QStringLiteral("interaction.menu"), regions,
            QJsonObject(), menus);
    }

    // 命令→对话框绑定（客户代码拥有，取代 ui.json 的 dialogBindings）。
    // 关于框：continuePolicy=Never，内容数据走 params（共享默认工厂从 params 读）。
    DialogCmdBinding aboutBinding(const QJsonObject& params)
    {
        DialogCmdBinding binding;
        binding.strCmd = QString::fromLatin1(about::kAboutCmdId);
        binding.strDlgId = QString::fromLatin1(about::kAboutDlgId);
        binding.params = params;
        binding.continuePolicy = DialogContinuePolicy::Never;
        return binding;
    }

    QJsonObject aboutParams()
    {
        return QJsonObject{
            { QStringLiteral("product"), QStringLiteral("Product A") },
            { QStringLiteral("version"), QStringLiteral("1.2.3") },
            { QStringLiteral("company"), QStringLiteral("Company A") }
        };
    }

    QByteArray aboutProfileJson()
    {
        QJsonArray regions{
            QJsonObject{ { QStringLiteral("id"), QStringLiteral("center") },
                { QStringLiteral("type"), QStringLiteral("CenterView") } }
        };
        return test::makeProfileJson(QStringLiteral("interaction.about"), regions);
    }

    // 命令表替身：新建后才能保存；网格是开关类命令。所有入口（菜单 / 工具栏 / 面板）都经它执行。
    class DocumentCommands final : public CommandTable
    {
    public:
        DocumentCommands()
        {
            add(QStringLiteral("file.new"), [this](const QJsonObject& args) {
                calls.append(QStringLiteral("file.new"));
                lastArgs = args;
                hasDocument = true;
            }).shortcut(QStringLiteral("Ctrl+N"));
            add(QStringLiteral("file.save"), [this](const QJsonObject&) { calls.append(QStringLiteral("file.save")); })
                .enabledWhen([this]() { return hasDocument; });
            add(QStringLiteral("view.grid"), [this](const QJsonObject&) { grid = !grid; })
                .checkedWhen([this]() { return grid; });
        }

        QStringList calls;
        QJsonObject lastArgs;
        bool hasDocument = false;
        bool grid = false;
    };

    // 菜单 + profile 工具栏引用同一组命令；工具栏里还有一个目录里没有的命令（应跳过）。
    QByteArray commandProfileJson(const QJsonArray& toolbarCommands)
    {
        const QJsonArray regions{
            QJsonObject{ { QStringLiteral("id"), QStringLiteral("menubar") },
                { QStringLiteral("type"), QStringLiteral("MenuBar") }, { QStringLiteral("order"), 5 } },
            QJsonObject{ { QStringLiteral("id"), QStringLiteral("top") },
                { QStringLiteral("type"), QStringLiteral("TopBar") }, { QStringLiteral("order"), 10 },
                { QStringLiteral("areas"), QJsonArray{ QJsonObject{ { QStringLiteral("id"), QStringLiteral("top.main") } } } } },
            QJsonObject{ { QStringLiteral("id"), QStringLiteral("center") },
                { QStringLiteral("type"), QStringLiteral("CenterView") }, { QStringLiteral("order"), 30 } }
        };
        const QJsonArray menus{ QJsonObject{ { QStringLiteral("type"), QStringLiteral("submenu") },
            { QStringLiteral("id"), QStringLiteral("file") },
            { QStringLiteral("titleKey"), QStringLiteral("File") },
            { QStringLiteral("order"), 10 },
            { QStringLiteral("children"),
                QJsonArray{ QStringLiteral("file.new"), QStringLiteral("file.save"), QStringLiteral("view.grid") } } } };
        const QJsonArray panels{ QJsonObject{ { QStringLiteral("id"), QStringLiteral("toolbar") },
            { QStringLiteral("factory"), QStringLiteral("msui.toolbar") },
            { QStringLiteral("area"), QStringLiteral("top.main") },
            { QStringLiteral("props"), QJsonObject{ { QStringLiteral("commands"), toolbarCommands } } } } };
        return test::makeProfileJson(
            QStringLiteral("interaction.commands"), regions, QJsonObject(), menus, panels);
    }

    QJsonArray defaultToolbarCommands()
    {
        return QJsonArray{ QStringLiteral("file.new"), QStringLiteral("|"), QStringLiteral("file.save"),
            QStringLiteral("view.grid"), QStringLiteral("unknown.command") };
    }

    QAction* menuAction(QWidget* shell, const QString& commandId)
    {
        auto* menu = shell ? shell->findChild<QMenu*>(QStringLiteral("msui.menu.file")) : nullptr;
        if (!menu)
            return nullptr;
        for (QAction* action : menu->actions())
        {
            if (action->objectName() == QStringLiteral("msui.action.") + commandId)
                return action;
        }
        return nullptr;
    }

    // 工具栏里的命令动作（不含分隔符），按出现顺序。
    QList<QAction*> toolbarActions(QWidget* shell)
    {
        QList<QAction*> result;
        auto* toolBar = shell ? shell->findChild<QToolBar*>(QStringLiteral("panel:toolbar")) : nullptr;
        if (!toolBar)
            return result;
        for (QAction* action : toolBar->actions())
        {
            if (!action->isSeparator())
                result.append(action);
        }
        return result;
    }

    // 只翻译一个 key 的文本提供者：其它 key 原样返回（与 QtTextProvider 缺译时的行为一致）。
    class OneKeyTextProvider final : public ITextProvider
    {
    public:
        QString text(const QString& key) const override
        {
            return key == QLatin1String("about.test.product") ? QStringLiteral("Translated product") : key;
        }
    };

    // 面板初始可见性来自 profile 的 defaultVisible 字段（区域用的是 visible）。
    QByteArray panelProfileJson()
    {
        QJsonArray regions{
            QJsonObject{ { QStringLiteral("id"), QStringLiteral("left") },
                { QStringLiteral("type"), QStringLiteral("LeftDock") },
                { QStringLiteral("order"), 20 },
                { QStringLiteral("areas"),
                    QJsonArray{ QJsonObject{ { QStringLiteral("id"),
                                                QStringLiteral("left.main") },
                        { QStringLiteral("preferredWidth"), 200 } } } } },
            QJsonObject{ { QStringLiteral("id"), QStringLiteral("center") },
                { QStringLiteral("type"), QStringLiteral("CenterView") },
                { QStringLiteral("order"), 30 } }
        };
        QJsonArray panels{
            QJsonObject{ { QStringLiteral("id"), QStringLiteral("tools") },
                { QStringLiteral("factory"), QStringLiteral("test.panel") },
                { QStringLiteral("area"), QStringLiteral("left.main") } },
            QJsonObject{ { QStringLiteral("id"), QStringLiteral("extra.tools") },
                { QStringLiteral("factory"), QStringLiteral("test.panel") },
                { QStringLiteral("area"), QStringLiteral("left.main") },
                { QStringLiteral("defaultVisible"), false } }
        };
        return test::makeProfileJson(QStringLiteral("interaction.panels"), regions,
            QJsonObject(), QJsonArray(), panels);
    }

    const char* const kLanguageProfile = R"json({
  "version": 2,
  "profileId": "interaction.i18n",
  "titleBar": { "titleKey": "mantisoft.title", "icon": "◆",
                "buttons": [ "close" ] },
  "regions": [
    { "id": "titlebar", "type": "TitleBar", "order": 1 },
    { "id": "menubar", "type": "MenuBar", "order": 5 },
    { "id": "center", "type": "CenterView", "order": 30 }
  ],
  "menus": [
    { "type": "submenu", "id": "file", "titleKey": "menu.file", "order": 10,
      "children": [ "file.new" ] }
  ]
})json";

    // 标题栏同步用例专用：包含最大化按钮，便于验证外部窗口状态被推回视图。
    const char* const kTitleBarProfile = R"json({
  "version": 2,
  "profileId": "interaction.titlebar",
  "titleBar": { "titleKey": "mantisoft.title", "icon": "◆",
                "buttons": [ "minimize", "maximize", "close" ] },
  "regions": [
    { "id": "titlebar", "type": "TitleBar", "order": 1 },
    { "id": "center", "type": "CenterView", "order": 30 }
  ]
})json";

    class FileCatalog : public ICommandCatalog
    {
    public:
        FileCatalog()
        {
            CommandView view;
            view.strId = QStringLiteral("file.new");
            view.strTitleKey = QStringLiteral("command.file.new");
            m_views.append(view);
        }

        const CommandView* find(const QString& commandId) const override
        {
            for (const CommandView& view : m_views)
            {
                if (view.strId == commandId)
                    return &view;
            }
            return nullptr;
        }


    private:
        QList<CommandView> m_views;
    };

    // 客户标题栏：只验证“视图契约 + 控件所有权”这条链路，不绘制任何东西。
    class RecordingTitleBar : public QWidget, public ITitleBarView
    {
    public:
        RecordingTitleBar(const TitleBarSpec& spec, QWidget* parent)
            : QWidget(parent)
            , m_spec(spec)
        {
        }

        QWidget* widget() override
        {
            return this;
        }

        const TitleBarSpec& spec() const override
        {
            return m_spec;
        }

        void setHost(ITitleBarHost* host) override
        {
            m_host = host;
        }

        void setMaximized(bool maximized) override
        {
            m_maximized = maximized;
            ++maximizedCalls;
        }

        bool isMaximizedShown() const override
        {
            return m_maximized;
        }

        QString titleText() const override
        {
            return m_spec.strTitle;
        }

        QStringList buttonIds() const override
        {
            return m_spec.listButtons;
        }

        // 装配路径不调用它：控件由 parent 持有，destroy() 只留给独立创建场景。
        void destroy() override
        {
            delete this;
        }

        ITitleBarHost* host() const
        {
            return m_host;
        }

        int maximizedCalls = 0;

    private:
        TitleBarSpec m_spec;
        ITitleBarHost* m_host = nullptr;
        bool m_maximized = false;
    };

    class RecordingTitleBarFactory : public ITitleBarFactory
    {
    public:
        QWidget* createTitleBar(const TitleBarSpec& spec, ITitleBarHost* host, QWidget* parent) override
        {
            Q_UNUSED(host);
            auto* bar = new RecordingTitleBar(spec, parent);
            m_last = bar;
            return bar;
        }

        QPointer<RecordingTitleBar> m_last;
    };

    // 自绘标题栏专用：显式关闭原生标题栏，确保 TitleBar 区域进入装配路径。
    const char* const kCustomTitleBarProfile = R"json({
  "version": 2,
  "profileId": "interaction.custom.titlebar",
  "window": { "nativeTitleBar": false },
  "titleBar": { "title": "Custom", "buttons": [ "close" ] },
  "regions": [
    { "id": "titlebar", "type": "TitleBar", "order": 1 },
    { "id": "center", "type": "CenterView", "order": 30 }
  ]
})json";

    void showShell(IShellRuntime& runtime)
    {
        QWidget* shell = runtime.shell();
        QVERIFY(shell != nullptr);
        shell->resize(800, 600);
        shell->show();
        QVERIFY(QTest::qWaitFor([shell]() {
            return shell->isVisible() && shell->windowHandle() != nullptr;
            }, 1000));
    }
} // namespace

class InteractionTest : public QObject
{
    Q_OBJECT

private slots:
    void menuExpandsAndTriggersCommand();
    void helpAboutOpensRegisteredDefaultDialog();
    void dialogBindingParamsAreResolved();
    void unboundCommandGoesToInvoker();
    void shellHostOpenDialogReturnsResult();
    void dialogParamsCarryCurrentTheme();
    void panelHostIsNarrowForwarder();
    void showMessageReachesStatusViews();
    void builtinMenusComeFromProfile();
    void dialogResultReachesCommandArgs();
    void panelVisibilityFollowsProfileAndActivate();
    void languageSwitchUpdatesMenusAndActions();
    void liveLanguageSwitchDoesNotCrash();
    void titleBarTracksExternalStateAfterRebuild();
    void titleBarDoubleClickTogglesMaximize();
    void titleBarViewContractSurvivesTakeShell();
    void lastErrorReflectsRealFailure();
    void customerStyleSheetLoadsFromQrc();
    void externalProfileBrandOverridesWindow();
    void externalProfileResolvesRelativeAssets();
    void externalProfileStylesheetOverridesTheme();
    void customerModuleIsUsable();
    void buildFingerprintsMatchHost();
    void customerDialogBindingsAreValid();
    void customerRegistersSampleDialog();
    void customerRegistersAboutDialog();
    void menuAndToolbarShareOneAction();
    void commandStateSyncsAllEntries();
    void sharedActionsSurviveShellRefresh();
    void panelInvokeWithArgsSkipsDialog();
    void toolbarPropsAreValidated();
    void dialogParamsCarryStyleSeries();
    void skinCoversSharedWidgets();
};

// 真实点击菜单栏展开下拉，再点击菜单项：全程走 QTest 事件，不直接 trigger。
void InteractionTest::menuExpandsAndTriggersCommand()
{
    const QByteArray profile = menuProfileJson();

    test::FakeCommandInvoker invoker;
    ShellServices shellServices;
    shellServices.cmdInvoker = &invoker;

    ApiPtr<IShellRuntime> runtime(createShellRuntime(shellServices));
    QVERIFY(runtime);
    QVERIFY2(runtime->loadFromJson(profile), qPrintable(runtime->lastError()));

    showShell(*runtime);

    auto* menuBar = runtime->shell()->findChild<QMenuBar*>(QStringLiteral("msui.menubar"));
    QVERIFY(menuBar != nullptr);
    QCOMPARE(menuBar->actions().size(), 1);

    QAction* fileMenuAction = menuBar->actions().at(0);
    QVERIFY(fileMenuAction->menu() != nullptr);
    QMenu* fileMenu = fileMenuAction->menu();

    QVERIFY(QTest::qWaitFor([menuBar, fileMenuAction]() {
        return !menuBar->actionGeometry(fileMenuAction).isEmpty();
        }, 1000));
    QTest::mouseClick(menuBar, Qt::LeftButton, Qt::NoModifier,
        menuBar->actionGeometry(fileMenuAction).center());
    QVERIFY(QTest::qWaitFor([fileMenu]() {
        return fileMenu->isVisible();
        }, 1000));

    // 命令动作是 shell 的共享动作（挂在 shell 下），菜单只是引用它：在菜单的动作列表里找。
    QAction* newAction = nullptr;
    for (QAction* action : fileMenu->actions())
    {
        if (action->objectName() == QLatin1String("msui.action.file.new"))
            newAction = action;
    }
    QVERIFY(newAction != nullptr);
    QCOMPARE(newAction->text(), QStringLiteral("file.new"));

    // offscreen 下菜单位置可能尚未完成布局，等 actionGeometry 可用再点击。
    QVERIFY(QTest::qWaitFor([fileMenu, newAction]() {
        return !fileMenu->actionGeometry(newAction).isEmpty();
        }, 1000));
    QTest::mouseClick(fileMenu, Qt::LeftButton, Qt::NoModifier,
        fileMenu->actionGeometry(newAction).center());

    QCOMPARE(invoker.invoked, QStringList{ QStringLiteral("file.new") });
}

// 关于框不是内置对话框：客户注册共享默认工厂，并在代码绑定表里把 help.about 绑到 dialog.about 才会打开它。
// 模态 exec() 会阻塞，必须靠定时器在对话框自己的事件循环里关闭它。
void InteractionTest::helpAboutOpensRegisteredDefaultDialog()
{
    about::AboutDialogFactory aboutFactory;
    ApiPtr<IDialogFactoryRegistry> dialogs(createDialogRegistry());
    QVERIFY(dialogs);
    dialogs->registerFactory(QString::fromLatin1(about::kAboutDlgId), &aboutFactory);

    ShellServices shellServices;
    shellServices.dlgReg = dialogs.get();
    shellServices.dialogBindings.append(aboutBinding(aboutParams()));
    ApiPtr<IShellRuntime> runtime(createShellRuntime(shellServices));
    QVERIFY(runtime);
    QVERIFY2(runtime->loadFromJson(aboutProfileJson()), qPrintable(runtime->lastError()));

    bool opened = false;
    bool modal = false;
    QString productText;
    QPointer<QWidget> dialogPointer;

    QTimer::singleShot(10, qApp, [&]() {
        for (QWidget* widget : QApplication::topLevelWidgets())
        {
            if (widget->objectName() != QLatin1String("msui.about.dialog"))
                continue;

            opened = true;
            dialogPointer = widget;

            if (auto* dialog = qobject_cast<QDialog*>(widget))
                modal = dialog->isModal();

            if (auto* product = widget->findChild<QLabel*>(
                QStringLiteral("about.product")))
            {
                productText = product->text();
            }

            widget->close();
        }
        });

    runtime->invoke(QString::fromLatin1(about::kAboutCmdId));

    QVERIFY(opened);
    QVERIFY(modal);
    QCOMPARE(productText, QStringLiteral("Product A"));
    // 模态循环结束后由工厂（runModalDialog）释放对话框，调用返回时指针必须已失效。
    QVERIFY(dialogPointer.isNull());
}

// params 的通用规则（不认识具体对话框）：xxxKey 翻译、缺译保留字面值、xxxPath 按 profile 目录解析、补 appVersion。
// 菜单入口（命令绑定）与宿主入口（IShellHost::openDialog）拿到同样处理过的 params。
void InteractionTest::dialogBindingParamsAreResolved()
{
    class RecordingFactory final : public IDialogFactory
    {
    public:
        DialogResult openDialog(QWidget* parent, const QJsonObject& params) override
        {
            ++called;
            received = params;
            parented = parent != nullptr;
            return DialogResult{};
        }

        int called = 0;
        QJsonObject received;
        bool parented = false;
    } factory;

    ApiPtr<IDialogFactoryRegistry> dialogs(createDialogRegistry());
    QVERIFY(dialogs);
    dialogs->registerFactory(QString::fromLatin1(about::kAboutDlgId), &factory);

    // xxxPath 的相对路径按 profile 所在目录解析：profile 写进临时目录再从文件加载。
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QJsonObject params{
        { QStringLiteral("productKey"), QStringLiteral("about.test.product") },
        { QStringLiteral("product"), QStringLiteral("Fallback product") },
        { QStringLiteral("companyKey"), QStringLiteral("about.test.missing") },
        { QStringLiteral("company"), QStringLiteral("Company A") },
        { QStringLiteral("logoPath"), QStringLiteral("assets/logo.png") },
        { QStringLiteral("absolutePath"), dir.filePath(QStringLiteral("abs.png")) }
    };
    const QJsonArray regions{ QJsonObject{ { QStringLiteral("id"), QStringLiteral("center") },
        { QStringLiteral("type"), QStringLiteral("CenterView") } } };
    const QString profilePath = dir.filePath(QStringLiteral("ui.json"));
    {
        QFile file(profilePath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(test::makeProfileJson(QStringLiteral("interaction.params"), regions));
    }

    const QString previousVersion = QCoreApplication::applicationVersion();
    QCoreApplication::setApplicationVersion(QStringLiteral("7.7.7"));

    OneKeyTextProvider text;
    ShellServices shellServices;
    shellServices.dlgReg = dialogs.get();
    shellServices.text = &text;
    shellServices.dialogBindings.append(aboutBinding(params));
    ApiPtr<IShellRuntime> runtime(createShellRuntime(shellServices));
    QVERIFY(runtime);
    QVERIFY2(runtime->loadFromFile(profilePath), qPrintable(runtime->lastError()));

    QMainWindow window;
    QVERIFY(runtime->attachWindow(&window));

    const auto verify = [&]() {
        QCOMPARE(factory.received.value(QStringLiteral("product")).toString(), QStringLiteral("Translated product"));
        QCOMPARE(factory.received.value(QStringLiteral("company")).toString(), QStringLiteral("Company A"));
        QCOMPARE(factory.received.value(QStringLiteral("logoPath")).toString(),
            QDir(dir.path()).absoluteFilePath(QStringLiteral("assets/logo.png")));
        QCOMPARE(factory.received.value(QStringLiteral("absolutePath")).toString(), dir.filePath(QStringLiteral("abs.png")));
        QCOMPARE(factory.received.value(QString::fromLatin1(DialogParam::AppVersion)).toString(), QStringLiteral("7.7.7"));
        QCOMPARE(factory.received.value(QString::fromLatin1(DialogParam::Theme)).toString(), QString::fromLatin1(ThemeId::Light));
        QVERIFY(factory.parented);
    };

    runtime->invoke(QString::fromLatin1(about::kAboutCmdId));
    QCOMPARE(factory.called, 1);
    verify();

    runtime->openDialog(QString::fromLatin1(about::kAboutDlgId), nullptr, params);
    QCOMPARE(factory.called, 2);
    verify();

    QCoreApplication::setApplicationVersion(previousVersion);
}

// shell 没有内置命令绑定：没有注册 help.about 的代码绑定时，即使注册了关于框也不会弹出，命令交给宿主调用器。
void InteractionTest::unboundCommandGoesToInvoker()
{
    about::AboutDialogFactory aboutFactory;
    ApiPtr<IDialogFactoryRegistry> dialogs(createDialogRegistry());
    QVERIFY(dialogs);
    dialogs->registerFactory(QString::fromLatin1(about::kAboutDlgId), &aboutFactory);

    test::FakeCommandInvoker invoker;
    ShellServices shellServices;
    shellServices.dlgReg = dialogs.get();
    shellServices.cmdInvoker = &invoker;
    ApiPtr<IShellRuntime> runtime(createShellRuntime(shellServices));
    QVERIFY(runtime);
    QVERIFY2(runtime->loadFromJson(test::minimalProfileJson(QStringLiteral("interaction.unbound"))),
        qPrintable(runtime->lastError()));

    // 关于框若意外弹出会阻塞 exec()：计时器随本函数析构，兜底关闭并记录。
    bool dialogShown = false;
    QTimer guard;
    guard.setSingleShot(true);
    QObject::connect(&guard, &QTimer::timeout, &guard, [&dialogShown]() {
        for (QWidget* widget : QApplication::topLevelWidgets())
        {
            if (widget->objectName() == QLatin1String("msui.about.dialog"))
            {
                dialogShown = true;
                widget->close();
            }
        }
    });
    guard.start(10);

    runtime->invoke(QString::fromLatin1(about::kAboutCmdId));
    QVERIFY(!dialogShown);
    QCOMPARE(invoker.invoked, QStringList{ QString::fromLatin1(about::kAboutCmdId) });
}

// IShellHost::openDialog 直接返回工厂给出的 DialogResult，params 原样透传；找不到工厂返回 Cancelled。
void InteractionTest::shellHostOpenDialogReturnsResult()
{
    class EchoDialogFactory final : public IDialogFactory
    {
    public:
        DialogResult openDialog(QWidget*, const QJsonObject& params) override
        {
            ++called;
            DialogResult result;
            result.status = DialogResult::Accepted;
            result.data = params;
            return result;
        }

        int called = 0;
    } factory;

    ApiPtr<IDialogFactoryRegistry> dialogs(createDialogRegistry());
    QVERIFY(dialogs);
    const DlgFactoryBinding binding = DlgFactoryBinding{ QStringLiteral("dialog.echo"), &factory };
    dialogs->registerFactory(binding.strDialogId, binding.factory);

    ShellServices shellServices;
    shellServices.dlgReg = dialogs.get();
    ApiPtr<IShellRuntime> runtime(createShellRuntime(shellServices));
    QVERIFY(runtime);

    IShellHost* host = runtime.get();
    const QJsonObject params{ { QStringLiteral("answer"), 42 } };
    const DialogResult result = host->openDialog(QStringLiteral("dialog.echo"), nullptr, params);
    QCOMPARE(factory.called, 1);
    QCOMPARE(int(result.status), int(DialogResult::Accepted));
    QCOMPARE(result.data.value(QStringLiteral("answer")).toInt(), 42);

    const DialogResult missing = host->openDialog(QStringLiteral("dialog.missing"), nullptr);
    QCOMPARE(int(missing.status), int(DialogResult::Cancelled));
    QCOMPARE(factory.called, 1);
    QVERIFY2(runtime->lastMessage().contains(QStringLiteral("dialog.missing")), qPrintable(runtime->lastMessage()));
}

// 打开对话框时 shell 补上当前皮肤（DialogParam::Theme）：切换皮肤后再打开拿到新值；调用方显式给的值不被覆盖。
void InteractionTest::dialogParamsCarryCurrentTheme()
{
    class RecordingThemeFactory final : public IDialogFactory
    {
    public:
        DialogResult openDialog(QWidget*, const QJsonObject& params) override
        {
            theme = params.value(QString::fromLatin1(DialogParam::Theme)).toString();
            return DialogResult{};
        }

        QString theme;
    } factory;

    ApiPtr<IDialogFactoryRegistry> dialogs(createDialogRegistry());
    QVERIFY(dialogs);
    dialogs->registerFactory(QStringLiteral("dialog.theme"), &factory);

    ShellServices shellServices;
    shellServices.dlgReg = dialogs.get();
    ApiPtr<IShellRuntime> runtime(createShellRuntime(shellServices));
    QVERIFY(runtime);
    QVERIFY2(runtime->loadFromJson(test::minimalProfileJson(QStringLiteral("interaction.dialogTheme"))),
        qPrintable(runtime->lastError()));

    runtime->openDialog(QStringLiteral("dialog.theme"), nullptr);
    QCOMPARE(factory.theme, QString::fromLatin1(ThemeId::Light));

    runtime->switchTheme(QString::fromLatin1(ThemeId::Dark));
    runtime->openDialog(QStringLiteral("dialog.theme"), nullptr);
    QCOMPARE(factory.theme, QString::fromLatin1(ThemeId::Dark));

    // 显式 params 优先：绑定 / 调用方写了 theme 时 shell 不改它。
    const QJsonObject pinned{ { QString::fromLatin1(DialogParam::Theme), QStringLiteral("pinned") } };
    runtime->openDialog(QStringLiteral("dialog.theme"), nullptr, pinned);
    QCOMPARE(factory.theme, QStringLiteral("pinned"));

    // 复位到浅色，避免影响同一进程里的后续用例。
    runtime->switchTheme(QString::fromLatin1(ThemeId::Light));
}

// 面板拿到的 IShellHost 是窄转发对象，不是运行期本身：转发照常工作，但不能向下转型拿到整个运行期。
void InteractionTest::panelHostIsNarrowForwarder()
{
    class CapturingFactory final : public IWidgetFactory
    {
    public:
        QWidget* create(const PanelCreateRequest& info) override
        {
            host = info.host;
            return new QWidget(info.parent);
        }

        IShellHost* host = nullptr;
    } factory;

    test::FakeWidgetFactoryRegistry registry;
    registry.registerFactory(QStringLiteral("test.capture"), &factory);

    test::FakeCommandInvoker invoker;
    ShellServices shellServices;
    shellServices.widgetReg = &registry;
    shellServices.cmdInvoker = &invoker;
    ApiPtr<IShellRuntime> runtime(createShellRuntime(shellServices));
    QVERIFY(runtime);

    const QJsonArray regions{
        QJsonObject{ { QStringLiteral("id"), QStringLiteral("left") },
            { QStringLiteral("type"), QStringLiteral("LeftDock") },
            { QStringLiteral("order"), 20 } },
        QJsonObject{ { QStringLiteral("id"), QStringLiteral("center") },
            { QStringLiteral("type"), QStringLiteral("CenterView") },
            { QStringLiteral("order"), 30 } } };
    const QJsonArray panels{
        QJsonObject{ { QStringLiteral("id"), QStringLiteral("probe") },
            { QStringLiteral("factory"), QStringLiteral("test.capture") },
            { QStringLiteral("area"), QStringLiteral("left.main") } } };
    const QByteArray profile =
        test::makeProfileJson(QStringLiteral("host-probe"), regions, QJsonObject(), QJsonArray(), panels);
    QVERIFY2(runtime->loadFromJson(profile), qPrintable(runtime->lastError()));

    QVERIFY(factory.host != nullptr);
    QVERIFY(factory.host != static_cast<IShellHost*>(runtime.get()));
    QVERIFY(dynamic_cast<IShellRuntime*>(factory.host) == nullptr);

    factory.host->invoke(QStringLiteral("probe.command"));
    QCOMPARE(invoker.invoked, QStringList{ QStringLiteral("probe.command") });
}

// IShellHost::showMessage：推给 shell 内实现 IStatusMessageView 的控件（通常是状态栏面板），并记入 lastMessage()。
void InteractionTest::showMessageReachesStatusViews()
{
    class StatusProbe final : public QLabel, public IStatusMessageView
    {
    public:
        explicit StatusProbe(QWidget* parent)
            : QLabel(parent)
        {
        }

        void showStatusMessage(const QString& text) override
        {
            setText(text);
        }
    };

    class StatusFactory final : public IWidgetFactory
    {
    public:
        QWidget* create(const PanelCreateRequest& info) override
        {
            host = info.host;
            auto* created = new StatusProbe(info.parent);
            probe = created;
            return created;
        }

        IShellHost* host = nullptr;
        QPointer<QLabel> probe;
    } factory;

    test::FakeWidgetFactoryRegistry registry;
    registry.registerFactory(QStringLiteral("test.status"), &factory);

    ShellServices shellServices;
    shellServices.widgetReg = &registry;
    ApiPtr<IShellRuntime> runtime(createShellRuntime(shellServices));
    QVERIFY(runtime);

    const QJsonArray regions{
        QJsonObject{ { QStringLiteral("id"), QStringLiteral("center") },
            { QStringLiteral("type"), QStringLiteral("CenterView") },
            { QStringLiteral("order"), 30 } },
        QJsonObject{ { QStringLiteral("id"), QStringLiteral("bottom") },
            { QStringLiteral("type"), QStringLiteral("StatusBar") },
            { QStringLiteral("order"), 50 } } };
    const QJsonArray panels{
        QJsonObject{ { QStringLiteral("id"), QStringLiteral("status") },
            { QStringLiteral("factory"), QStringLiteral("test.status") },
            { QStringLiteral("area"), QStringLiteral("bottom.main") } } };
    const QByteArray profile =
        test::makeProfileJson(QStringLiteral("status-probe"), regions, QJsonObject(), QJsonArray(), panels);
    QVERIFY2(runtime->loadFromJson(profile), qPrintable(runtime->lastError()));

    QVERIFY(factory.host != nullptr);
    QVERIFY(factory.probe);

    factory.host->showMessage(QStringLiteral("Saved"));
    QCOMPARE(factory.probe->text(), QStringLiteral("Saved"));
    QCOMPARE(runtime->lastMessage(), QStringLiteral("Saved"));
}

// 语言 / 皮肤子菜单由 profile 的 builtin 节点声明，放在哪个菜单由 profile 决定；help 菜单不再被自动插入。
// 没有翻译服务时 language 节点不生成子菜单（无法切换语言）；未知 builtin id 是 profile 错误。
void InteractionTest::builtinMenusComeFromProfile()
{
    const QJsonArray regions{
        QJsonObject{ { QStringLiteral("id"), QStringLiteral("menubar") },
            { QStringLiteral("type"), QStringLiteral("MenuBar") },
            { QStringLiteral("order"), 5 } },
        QJsonObject{ { QStringLiteral("id"), QStringLiteral("center") },
            { QStringLiteral("type"), QStringLiteral("CenterView") },
            { QStringLiteral("order"), 30 } } };
    const QJsonObject themeNode{ { QStringLiteral("type"), QStringLiteral("builtin") },
        { QStringLiteral("id"), QStringLiteral("theme") } };
    const QJsonObject languageNode{ { QStringLiteral("type"), QStringLiteral("builtin") },
        { QStringLiteral("id"), QStringLiteral("language") } };
    const QJsonArray menus{
        QJsonObject{ { QStringLiteral("id"), QStringLiteral("view") },
            { QStringLiteral("type"), QStringLiteral("submenu") },
            { QStringLiteral("titleKey"), QStringLiteral("menu.view") },
            { QStringLiteral("order"), 10 },
            { QStringLiteral("children"), QJsonArray{ themeNode, languageNode } } },
        QJsonObject{ { QStringLiteral("id"), QStringLiteral("help") },
            { QStringLiteral("type"), QStringLiteral("submenu") },
            { QStringLiteral("titleKey"), QStringLiteral("menu.help") },
            { QStringLiteral("order"), 20 },
            { QStringLiteral("children"), QJsonArray{ QString::fromLatin1(about::kAboutCmdId) } } } };

    ShellServices shellServices;
    ApiPtr<IShellRuntime> runtime(createShellRuntime(shellServices));
    QVERIFY(runtime);
    QVERIFY2(runtime->loadFromJson(test::makeProfileJson(QStringLiteral("builtin-menus"), regions, QJsonObject(), menus)),
        qPrintable(runtime->lastError()));

    QWidget* shell = runtime->shell();
    QVERIFY(shell != nullptr);

    auto* view = shell->findChild<QMenu*>(QStringLiteral("msui.menu.view"));
    QVERIFY(view != nullptr);
    QVERIFY(view->findChild<QMenu*>(QStringLiteral("msui.menu.theme")) != nullptr);
    QVERIFY(shell->findChild<QMenu*>(QStringLiteral("msui.menu.language")) == nullptr);

    auto* help = shell->findChild<QMenu*>(QStringLiteral("msui.menu.help"));
    QVERIFY(help != nullptr);
    QVERIFY(help->findChild<QMenu*>(QStringLiteral("msui.menu.theme")) == nullptr);

    const QJsonArray badMenus{
        QJsonObject{ { QStringLiteral("id"), QStringLiteral("view") },
            { QStringLiteral("type"), QStringLiteral("submenu") },
            { QStringLiteral("children"),
                QJsonArray{ QJsonObject{ { QStringLiteral("type"), QStringLiteral("builtin") },
                    { QStringLiteral("id"), QStringLiteral("bogus") } } } } } };
    QVERIFY(!runtime->loadFromJson(test::makeProfileJson(QStringLiteral("bad-builtin"), regions, QJsonObject(), badMenus)));
    QVERIFY2(runtime->lastError().contains(QStringLiteral("builtin")), qPrintable(runtime->lastError()));
}

// 代码绑定表：对话框 Accepted 后，DialogResult.data 作为 args 交给 ICommandInvoker::invokeWithArgs；
// 取消或 continuePolicy=Never 时不执行命令；没有绑定的命令照旧走 invoke(id)；
// 未覆盖 invokeWithArgs 的旧调用器经默认实现退回 invoke(id)。
void InteractionTest::dialogResultReachesCommandArgs()
{
    // 参数对话框替身：把 params 原样带回，再加一个 "confirmed" 字段；accept=false 模拟用户取消。
    class ParamDialogFactory final : public IDialogFactory
    {
    public:
        DialogResult openDialog(QWidget*, const QJsonObject& params) override
        {
            ++opened;
            DialogResult result;
            result.status = accept ? DialogResult::Accepted : DialogResult::Cancelled;
            if (accept)
            {
                result.data = params;
                result.data.insert(QStringLiteral("confirmed"), true);
            }
            return result;
        }

        bool accept = true;
        int opened = 0;
    } dialog;

    class RecordingArgsInvoker final : public ICommandInvoker
    {
    public:
        void invoke(const QString& commandId) override
        {
            plain.append(commandId);
        }

        void invokeWithArgs(const QString& commandId, const QJsonObject& args) override
        {
            withArgs.append(commandId);
            lastArgs = args;
        }

        QStringList plain;
        QStringList withArgs;
        QJsonObject lastArgs;
    } invoker;

    ApiPtr<IDialogFactoryRegistry> dialogs(createDialogRegistry());
    QVERIFY(dialogs);
    const DlgFactoryBinding binding = DlgFactoryBinding{ QStringLiteral("dialog.algo"), &dialog };
    dialogs->registerFactory(binding.strDialogId, binding.factory);

    const QByteArray profile = test::makeProfileJson(QStringLiteral("dialog-args"),
        QJsonArray{ QJsonObject{ { QStringLiteral("id"), QStringLiteral("center") },
            { QStringLiteral("type"), QStringLiteral("CenterView") },
            { QStringLiteral("order"), 30 } } });

    ShellServices shellServices;
    shellServices.dlgReg = dialogs.get();
    shellServices.cmdInvoker = &invoker;
    shellServices.dialogBindings.append(DialogCmdBinding{
        QStringLiteral("algo.run"), QStringLiteral("dialog.algo"),
        QJsonObject{ { QStringLiteral("threshold"), 3 } }, DialogContinuePolicy::OnAccepted });
    shellServices.dialogBindings.append(DialogCmdBinding{
        QStringLiteral("algo.preview"), QStringLiteral("dialog.algo"),
        QJsonObject{}, DialogContinuePolicy::Never });
    ApiPtr<IShellRuntime> runtime(createShellRuntime(shellServices));
    QVERIFY(runtime);
    QVERIFY2(runtime->loadFromJson(profile), qPrintable(runtime->lastError()));

    // 1) 确认：参数到达命令。
    runtime->invoke(QStringLiteral("algo.run"));
    QCOMPARE(dialog.opened, 1);
    QCOMPARE(invoker.withArgs, QStringList{ QStringLiteral("algo.run") });
    QCOMPARE(invoker.lastArgs.value(QStringLiteral("threshold")).toInt(), 3);
    QVERIFY(invoker.lastArgs.value(QStringLiteral("confirmed")).toBool());
    QVERIFY(invoker.plain.isEmpty());

    // 2) 取消：不执行命令。
    dialog.accept = false;
    runtime->invoke(QStringLiteral("algo.run"));
    QCOMPARE(dialog.opened, 2);
    QCOMPARE(invoker.withArgs.size(), 1);

    // 3) continuePolicy=Never：对话框即命令全部行为。
    dialog.accept = true;
    runtime->invoke(QStringLiteral("algo.preview"));
    QCOMPARE(dialog.opened, 3);
    QCOMPARE(invoker.withArgs.size(), 1);

    // 4) 没有绑定的命令照旧走 invoke(id)。
    runtime->invoke(QStringLiteral("file.save"));
    QCOMPARE(invoker.plain, QStringList{ QStringLiteral("file.save") });

    // 5) 只实现 invoke(id) 的旧调用器：默认 invokeWithArgs 退回 invoke(id)，现有客户无需改动。
    test::FakeCommandInvoker legacy;
    ShellServices legacyServices;
    legacyServices.dlgReg = dialogs.get();
    legacyServices.cmdInvoker = &legacy;
    legacyServices.dialogBindings = shellServices.dialogBindings;
    ApiPtr<IShellRuntime> legacyRuntime(createShellRuntime(legacyServices));
    QVERIFY(legacyRuntime);
    QVERIFY2(legacyRuntime->loadFromJson(profile), qPrintable(legacyRuntime->lastError()));
    legacyRuntime->invoke(QStringLiteral("algo.run"));
    QCOMPARE(legacy.invoked, QStringList{ QStringLiteral("algo.run") });
}

// 面板初始可见性由 profile 决定；activatePanel 对已挂载面板重新 show + raise。
void InteractionTest::panelVisibilityFollowsProfileAndActivate()
{
    test::FakeWidgetFactoryRegistry registry;
    test::FakeWidgetFactory factory;
    registry.registerFactory(QStringLiteral("test.panel"), &factory);

    ShellServices shellServices;
    shellServices.widgetReg = &registry;

    ApiPtr<IShellRuntime> runtime(createShellRuntime(shellServices));
    QVERIFY(runtime);
    QVERIFY2(runtime->loadFromJson(panelProfileJson()),
        qPrintable(runtime->lastError()));

    showShell(*runtime);

    IRegionContainer* left = runtime->regionContainer(QStringLiteral("left"));
    QVERIFY(left != nullptr);

    QWidget* tools = left->panelWidget(QStringLiteral("tools"));
    QVERIFY(tools != nullptr);
    QVERIFY(tools->isVisible());

    // defaultVisible=false 的面板不参与装配，因此不会被任何区域承载。
    QVERIFY(left->panelWidget(QStringLiteral("extra.tools")) == nullptr);
    QVERIFY(runtime->regionContainerForPanel(QStringLiteral("extra.tools")) == nullptr);
    QVERIFY(!runtime->activatePanel(QStringLiteral("extra.tools")));

    // 已挂载面板被隐藏后，activatePanel 负责重新显示它。
    tools->hide();
    QVERIFY(!tools->isVisible());
    QVERIFY(runtime->activatePanel(QStringLiteral("tools")));
    QVERIFY(tools->isVisible());
}

// 诊断用例：对已加载的运行期反复调用 switchLanguage，覆盖 install / refreshShell / retranslate 全链路。
// 这里用通用 widget 面板（FakeWidgetFactory，不含 QML）：若此处崩溃，说明是 shell 语言切换机制的通用问题；
// 若此处通过而 GoodLaser（QML 面板）崩溃，则问题在 QML 面板的重翻译路径。
void InteractionTest::liveLanguageSwitchDoesNotCrash()
{
    test::FakeCommandInvoker invoker;
    ApiPtr<ITranslationService> trans(createTranslationService());
    QVERIFY(trans);

    ShellServices shellServices;
    shellServices.cmdInvoker = &invoker;
    shellServices.transService = trans.get();

    ApiPtr<IShellRuntime> runtime(createShellRuntime(shellServices));
    QVERIFY(runtime);
    QVERIFY2(runtime->loadFromJson(commandProfileJson(QJsonArray{ QStringLiteral("file.new") })),
        qPrintable(runtime->lastError()));

    showShell(*runtime);

    runtime->switchLanguage(QStringLiteral("zh_CN"));
    QApplication::processEvents();
    runtime->switchLanguage(QStringLiteral("en_US"));
    QApplication::processEvents();
    runtime->switchLanguage(QStringLiteral("ja_JP"));
    QApplication::processEvents();
    runtime->switchLanguage(QStringLiteral("en_US"));
    QApplication::processEvents();

    QVERIFY(true);
}

// 同一份 profile 在 en_US / zh_CN 下重建，标题栏与菜单/命令文字必须同步切换。
// 自绘标题栏仅存在于 Windows；非 Windows 原生标题栏不生成自绘控件。
void InteractionTest::languageSwitchUpdatesMenusAndActions()
{
#if defined(Q_OS_WIN)
    FileCatalog catalog;
    ApiPtr<IQtTextProvider> text(createQtTextProvider());
    const QByteArray profile = QByteArray(kLanguageProfile);

    QString enTitle;
    QString enMenu;
    QString enAction;
    {
        test::TranslationGuard guard(QStringLiteral("en_US"));
        QVERIFY2(guard.isInstalled(), qPrintable(guard.lastError()));

        ShellServices shellServices;
        shellServices.cmdCatalog = &catalog;
        shellServices.text = text.get();

        ApiPtr<IShellRuntime> runtime(createShellRuntime(shellServices));
        QVERIFY(runtime);
        QVERIFY2(runtime->loadFromJson(profile), qPrintable(runtime->lastError()));

        auto* title = runtime->shell()->findChild<QLabel*>(
            QStringLiteral("msui.titlebar.title"));
        QVERIFY(title != nullptr);
        enTitle = title->text();

        auto* menuBar = runtime->shell()->findChild<QMenuBar*>(
            QStringLiteral("msui.menubar"));
        QVERIFY(menuBar != nullptr);
        QCOMPARE(menuBar->actions().size(), 1);
        enMenu = menuBar->actions().at(0)->text();

        auto* action = runtime->shell()->findChild<QAction*>(
            QStringLiteral("msui.action.file.new"));
        QVERIFY(action != nullptr);
        enAction = action->text();
    }
    QCOMPARE(enTitle, QStringLiteral("MantiSoft · Manufacturing Workbench"));
    QCOMPARE(enMenu, QStringLiteral("File"));
    QCOMPARE(enAction, QStringLiteral("New"));

    QString zhTitle;
    QString zhMenu;
    QString zhAction;
    {
        test::TranslationGuard guard(QStringLiteral("zh_CN"));
        QVERIFY2(guard.isInstalled(), qPrintable(guard.lastError()));

        ShellServices shellServices;
        shellServices.cmdCatalog = &catalog;
        shellServices.text = text.get();

        ApiPtr<IShellRuntime> runtime(createShellRuntime(shellServices));
        QVERIFY(runtime);
        QVERIFY2(runtime->loadFromJson(profile), qPrintable(runtime->lastError()));

        auto* title = runtime->shell()->findChild<QLabel*>(
            QStringLiteral("msui.titlebar.title"));
        QVERIFY(title != nullptr);
        zhTitle = title->text();

        auto* menuBar = runtime->shell()->findChild<QMenuBar*>(
            QStringLiteral("msui.menubar"));
        QVERIFY(menuBar != nullptr);
        QCOMPARE(menuBar->actions().size(), 1);
        zhMenu = menuBar->actions().at(0)->text();

        auto* action = runtime->shell()->findChild<QAction*>(
            QStringLiteral("msui.action.file.new"));
        QVERIFY(action != nullptr);
        zhAction = action->text();
    }

    // 翻译数据是中文，但源码里用 \u 转义，满足代码区禁止字面 CJK 的门禁。
    QCOMPARE(zhTitle, QStringLiteral("MantiSoft \u00b7 \u5236\u9020\u5de5\u4f5c\u53f0"));
    QCOMPARE(zhMenu, QStringLiteral("\u6587\u4ef6"));
    QCOMPARE(zhAction, QStringLiteral("\u65b0\u5efa"));
    QVERIFY(enTitle != zhTitle);
    QVERIFY(enMenu != zhMenu);
    QVERIFY(enAction != zhAction);

    // 两个 TranslationGuard 均已析构：缺译时文本 id 原样返回。
    ApiPtr<IQtTextProvider> noTranslator(createQtTextProvider());
    QCOMPARE(noTranslator->text(QStringLiteral("menu.file")),
        QStringLiteral("menu.file"));
#else
    QSKIP("self-drawn title bar is Windows-only");
#endif
}

// 标题栏不再自己监听窗口：外部最大化与 rebuild() 后的新视图，
// 都必须由 TitleBarHostAdapter 重新绑定并推送当前状态。
// 自绘标题栏仅存在于 Windows。
void InteractionTest::titleBarTracksExternalStateAfterRebuild()
{
#if defined(Q_OS_WIN)
    ShellServices shellServices;

    ApiPtr<IShellRuntime> runtime(createShellRuntime(shellServices));
    QVERIFY(runtime);
    QVERIFY2(runtime->loadFromJson(QByteArray(kTitleBarProfile)), qPrintable(runtime->lastError()));

    QMainWindow window;
    QVERIFY(runtime->attachWindow(&window));
    window.resize(800, 600);
    window.show();
    QVERIFY(QTest::qWaitFor([&window]() {
        return window.isVisible() && window.windowHandle() != nullptr;
        }, 1000));

    const auto findMaximize = [&window]() -> QAbstractButton* {
        return window.findChild<QAbstractButton*>(QStringLiteral("msui.titlebar.button.maximize"));
    };

    QAbstractButton* maximize = findMaximize();
    QVERIFY(maximize != nullptr);
    QCOMPARE(maximize->text(), QStringLiteral("\u25A1"));

    window.showMaximized();
    QVERIFY(QTest::qWaitFor([&window]() { return window.isMaximized(); }, 1000));
    QVERIFY(QTest::qWaitFor([maximize]() { return maximize->text() == QStringLiteral("\u2750"); }, 1000));

    // 全量重建删除旧标题栏；适配器必须重绑新视图并立刻同步最大化字形。
    QVERIFY2(runtime->loadFromJson(QByteArray(kTitleBarProfile)), qPrintable(runtime->lastError()));
    QAbstractButton* rebuiltMaximize = findMaximize();
    QVERIFY(rebuiltMaximize != nullptr);
    QCOMPARE(rebuiltMaximize->text(), QStringLiteral("\u2750"));

    window.showNormal();
    QVERIFY(QTest::qWaitFor([&window]() { return !window.isMaximized(); }, 1000));
    QVERIFY(QTest::qWaitFor([rebuiltMaximize]() {
        return rebuiltMaximize->text() == QStringLiteral("\u25A1");
        }, 1000));
#else
    QSKIP("self-drawn title bar is Windows-only");
#endif
}

// 双击标题栏必须经过 ITitleBarHost::requestToggleMaximize 走真实窗口路径，
// 而不是由视图自己 showMaximized()（那样客户换标题栏实现就会失效）。
// 自绘标题栏仅存在于 Windows。
void InteractionTest::titleBarDoubleClickTogglesMaximize()
{
#if defined(Q_OS_WIN)
    ShellServices shellServices;

    ApiPtr<IShellRuntime> runtime(createShellRuntime(shellServices));
    QVERIFY(runtime);
    QVERIFY2(runtime->loadFromJson(QByteArray(kTitleBarProfile)), qPrintable(runtime->lastError()));

    QMainWindow window;
    QVERIFY(runtime->attachWindow(&window));
    window.resize(800, 600);
    window.show();
    QVERIFY(QTest::qWaitFor([&window]() {
        return window.isVisible() && window.windowHandle() != nullptr;
        }, 1000));

    QWidget* titleBar = window.findChild<QWidget*>(QStringLiteral("msui.titlebar"));
    QVERIFY(titleBar != nullptr);

    QTest::mouseDClick(titleBar, Qt::LeftButton, Qt::NoModifier, titleBar->rect().center());
    QVERIFY(QTest::qWaitFor([&window]() { return window.isMaximized(); }, 1000));

    QAbstractButton* maximize = window.findChild<QAbstractButton*>(
        QStringLiteral("msui.titlebar.button.maximize"));
    QVERIFY(maximize != nullptr);
    QVERIFY(QTest::qWaitFor([maximize]() {
        return maximize->text() == QStringLiteral("\u2750");
        }, 1000));

    QTest::mouseDClick(titleBar, Qt::LeftButton, Qt::NoModifier, titleBar->rect().center());
    QVERIFY(QTest::qWaitFor([&window]() { return !window.isMaximized(); }, 1000));
    QVERIFY(QTest::qWaitFor([maximize]() {
        return maximize->text() == QStringLiteral("\u25A1");
        }, 1000));
#else
    QSKIP("self-drawn title bar is Windows-only");
#endif
}

// 标题栏视图的接口指针必须保留，不能从 QWidget 反推；
// takeShell() 后 shell 仍存活，适配器必须先把视图的 host 清空再交出所有权。
void InteractionTest::titleBarViewContractSurvivesTakeShell()
{
#if defined(Q_OS_WIN)
    RecordingTitleBarFactory factory;
    ShellServices shellServices;
    shellServices.titleBarFactory = &factory;

    ApiPtr<IShellRuntime> runtime(createShellRuntime(shellServices));
    QVERIFY(runtime);
    QVERIFY2(runtime->loadFromJson(QByteArray(kCustomTitleBarProfile)), qPrintable(runtime->lastError()));

    QMainWindow window;
    QVERIFY(runtime->attachWindow(&window));

    // 视图契约没有丢：客户控件同时实现 ITitleBarView 时能收到宿主与最大化状态。
    QVERIFY(factory.m_last);
    QVERIFY(factory.m_last->host() != nullptr);
    QVERIFY(factory.m_last->maximizedCalls > 0);

    QWidget* shell = runtime->takeShell();
    QVERIFY(shell);

    // P0-4：所有权交出去后标题栏仍可点击，不能继续持有适配器。
    QVERIFY(factory.m_last->host() == nullptr);
    QVERIFY(shell->isWidgetType());
#else
    QSKIP("self-drawn title bar is Windows-only");
#endif
}

// 加载成功时 lastError() 必须为空；文件打不开时不能被随后的
// setAssemblyDiagnostics() 覆盖成 "profile rejected"。
void InteractionTest::lastErrorReflectsRealFailure()
{
    ShellServices shellServices;
    ApiPtr<IShellRuntime> runtime(createShellRuntime(shellServices));
    QVERIFY(runtime);

    QVERIFY2(runtime->loadFromJson(test::minimalProfileJson(QStringLiteral("interaction.lastError"))),
        qPrintable(runtime->lastError()));
    QVERIFY2(runtime->lastError().isEmpty(), qPrintable(runtime->lastError()));

    const QString missing = QStringLiteral("Z:/msui-no-such-dir/interaction.json");
    QVERIFY(!runtime->loadFromFile(missing));
    QVERIFY2(runtime->lastError().contains(QStringLiteral("cannot open")),
        qPrintable(runtime->lastError()));
    QVERIFY2(runtime->lastError().contains(missing), qPrintable(runtime->lastError()));
}

// customStylePrefix 是 qrc prefix（/UiCustomerX），ThemeController 必须补 ":"；
// 漏掉时 QFile 会按文件系统绝对路径查找，客户皮肤永远不生效。
void InteractionTest::customerStyleSheetLoadsFromQrc()
{
    ShellServices shellServices;
    shellServices.strCustomStyle = QStringLiteral("/UiTestCustomerStyle");

    ApiPtr<IShellRuntime> runtime(createShellRuntime(shellServices));
    QVERIFY(runtime);

    // 客户只提供 dark.qss，light 应回退框架默认。
    QVERIFY(!qApp->styleSheet().contains(QStringLiteral("msui-test-customer-style")));

    runtime->switchTheme(QStringLiteral("dark"));
    QVERIFY2(qApp->styleSheet().contains(QStringLiteral("msui-test-customer-style")),
        qPrintable(qApp->styleSheet()));

    // 复位到框架浅色，避免影响同一进程里的后续用例。
    runtime->switchTheme(QStringLiteral("light"));
    QVERIFY(!qApp->styleSheet().contains(QStringLiteral("msui-test-customer-style")));
}

// M5：外置 ui.json 的 brand 段覆盖客户模块写死的标题/皮肤，无需重编客户库。
void InteractionTest::externalProfileBrandOverridesWindow()
{
    const QByteArray json = R"json({
  "version": 2,
  "profileId": "interaction.brand",
  "brand": {
    "product": "Site Product",
    "theme": "dark"
  },
  "regions": [ { "id": "center", "type": "CenterView", "order": 30 } ]
})json";

    ShellServices shellServices;
    ApiPtr<IShellRuntime> runtime(createShellRuntime(shellServices));
    QVERIFY(runtime);
    QVERIFY2(runtime->loadFromJson(json), qPrintable(runtime->lastError()));
    QCOMPARE(runtime->theme(), QStringLiteral("dark"));

    QMainWindow window;
    window.setWindowTitle(QStringLiteral("Module Default Title"));
    QVERIFY(runtime->attachWindow(&window));
    QCOMPARE(window.windowTitle(), QStringLiteral("Site Product"));

    // 复位浅色，避免影响后续用例的全局样式表。
    runtime->switchTheme(QStringLiteral("light"));
}

// M5：外置 profile 的相对图标按 ui.json 所在目录解析（brand 与自绘标题栏都生效）。
void InteractionTest::externalProfileResolvesRelativeAssets()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QDir root(dir.path());
    QVERIFY(root.mkpath(QStringLiteral("assets")));

    // 1x1 PNG：只验证图标能从相对路径加载，不依赖 SVG 插件。
    const QByteArray png = QByteArray::fromBase64(
        "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==");
    QFile logo(root.absoluteFilePath(QStringLiteral("assets/logo.png")));
    QVERIFY(logo.open(QIODevice::WriteOnly));
    QCOMPARE(logo.write(png), png.size());
    logo.close();

    const QByteArray json = R"json({
  "version": 2,
  "profileId": "interaction.assets",
  "brand": { "product": "Site Window", "icon": "assets/logo.png" },
  "titleBar": { "title": "Ignored Title", "icon": "assets/logo.png" },
  "regions": [ { "id": "center", "type": "CenterView", "order": 30 } ]
})json";
    QFile profile(root.absoluteFilePath(QStringLiteral("ui.json")));
    QVERIFY(profile.open(QIODevice::WriteOnly));
    QCOMPARE(profile.write(json), json.size());
    profile.close();

    ShellServices shellServices;
    ApiPtr<IShellRuntime> runtime(createShellRuntime(shellServices));
    QVERIFY(runtime);
    QVERIFY2(runtime->loadFromFile(profile.fileName()), qPrintable(runtime->lastError()));

    QMainWindow window;
    QVERIFY(runtime->attachWindow(&window));
    QCOMPARE(window.windowTitle(), QStringLiteral("Site Window"));
    QVERIFY(!window.windowIcon().isNull());
}

// M5：外置 profile 的 styles/<id>.qss 覆盖内置皮肤，无需重编客户库。
void InteractionTest::externalProfileStylesheetOverridesTheme()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QDir root(dir.path());
    QVERIFY(root.mkpath(QStringLiteral("styles")));
    const QByteArray marker = "/* msui-external-profile-style */";

    QFile sheet(root.absoluteFilePath(QStringLiteral("styles/dark.qss")));
    QVERIFY(sheet.open(QIODevice::WriteOnly));
    QCOMPARE(sheet.write(marker), marker.size());
    sheet.close();

    ShellServices shellServices;
    shellServices.strStyleDir = dir.path();
    ApiPtr<IShellRuntime> runtime(createShellRuntime(shellServices));
    QVERIFY(runtime);

    runtime->switchTheme(QStringLiteral("dark"));
    QCOMPARE(runtime->theme(), QStringLiteral("dark"));
    QVERIFY(qApp->styleSheet().contains(QStringLiteral("msui-external-profile-style")));

    // 复位浅色，避免影响同一进程里的后续用例。
    runtime->switchTheme(QStringLiteral("light"));
    QVERIFY(!qApp->styleSheet().contains(QStringLiteral("msui-external-profile-style")));
}

// 客户模块入口：返回可用的模块描述。
void InteractionTest::customerModuleIsUsable()
{
    const CustomerModule module = createCustomerModule();
    ApiPtr<IApiObject> owner(module.owner);  // 测试代替宿主接管部件持有者，用例结束时在客户库内释放
    QVERIFY(!module.strId.isEmpty());
    QVERIFY(!module.strProfilePath.isEmpty());
    // 品牌系列只在客户模块声明一次：对话框经 params 拿到它套用品牌色。
    QVERIFY(!module.strStyleSeries.isEmpty());

    // 所有部件都由 owner 持有：客户不使用全局变量，宿主负责在运行期销毁后释放。
    QVERIFY(module.owner != nullptr);
    // 命令表同时提供目录、执行与状态：菜单、工具栏、面板按钮共用同一套。
    QVERIFY(module.cmdCatalog != nullptr);
    QVERIFY(module.cmdInvoker != nullptr);
    QVERIFY(module.cmdState != nullptr);
    QVERIFY(module.cmdProfileRegistrar != nullptr);  // 外置 --profile 的菜单命令可补登记

    // strProfilePath 指向客户库内嵌的 profile 资源：换机器 / 漏拷外置文件时仍能兜底启动。
    QVERIFY2(module.strProfilePath.startsWith(QLatin1Char(':')), qPrintable(module.strProfilePath));
    QVERIFY2(QFile::exists(module.strProfilePath), qPrintable(module.strProfilePath));

    // 显示名可按当前语言重算：语言切换刷新窗口标题时不再重新创建客户模块。
    QVERIFY(module.displayNameFn != nullptr);
    QCOMPARE(module.displayNameFn(), module.strDisplayName);
}

// 二进制兼容只靠构建指纹保证：同一次构建出的 EXE / shell / 客户库指纹必须逐字节一致，
// 且指纹里带着当前契约代际 kUiApiVersion。
void InteractionTest::buildFingerprintsMatchHost()
{
    const QByteArray host(hostBuildFingerprint());
    QVERIFY(!host.isEmpty());
    QCOMPARE(QByteArray(customerBuildFingerprint()), host);
    QCOMPARE(QByteArray(shellBuildFingerprint()), host);
    QVERIFY2(host.contains(QByteArray("|api=") + QByteArray::number(kUiApiVersion) + '|'), host.constData());
}

// 客户注册的对话框工厂与命令→对话框绑定都合法：同 id 即可覆盖共享默认实现。
void InteractionTest::customerDialogBindingsAreValid()
{
    const CustomerModule module = createCustomerModule();
    ApiPtr<IApiObject> owner(module.owner);  // 测试代替宿主接管部件持有者，用例结束时在客户库内释放
    for (const DlgFactoryBinding& binding : module.listDlgFactoriesBind)
    {
        QVERIFY2(!binding.strDialogId.isEmpty(), "dialog id must not be empty");
        QVERIFY2(binding.factory != nullptr, qPrintable(binding.strDialogId));
    }
    for (const DialogCmdBinding& binding : module.listDialogBind)
    {
        QVERIFY2(!binding.strCmd.isEmpty(), "bound command id must not be empty");
        QVERIFY2(!binding.strDlgId.isEmpty(), qPrintable(binding.strCmd));
    }
}

// 示例链路：每个客户都注册 dialog.sample.params，并在命令目录里声明 sample.run。
void InteractionTest::customerRegistersSampleDialog()
{
    const CustomerModule module = createCustomerModule();
    ApiPtr<IApiObject> owner(module.owner);  // 测试代替宿主接管部件持有者，用例结束时在客户库内释放
    bool registered = false;
    for (const DlgFactoryBinding& binding : module.listDlgFactoriesBind)
    {
        if (binding.strDialogId == QLatin1String("dialog.sample.params"))
            registered = true;
    }
    QVERIFY(registered);
    QVERIFY(module.cmdCatalog != nullptr);
    QVERIFY(module.cmdCatalog->find(QStringLiteral("sample.run")) != nullptr);
}

// 关于框不是内置对话框：每个客户都必须以 dialog.about 注册一个工厂（共享默认实现或自己的界面）。
void InteractionTest::customerRegistersAboutDialog()
{
    const CustomerModule module = createCustomerModule();
    ApiPtr<IApiObject> owner(module.owner);  // 测试代替宿主接管部件持有者，用例结束时在客户库内释放
    bool registered = false;
    for (const DlgFactoryBinding& binding : module.listDlgFactoriesBind)
    {
        if (binding.strDialogId == QLatin1String(about::kAboutDlgId))
            registered = binding.factory != nullptr;
    }
    QVERIFY(registered);
}


// 一个功能 = 一个命令 id：菜单项、profile 工具栏按钮用的是同一个 QAction，只连接一次，快捷键挂在 shell 上。
void InteractionTest::menuAndToolbarShareOneAction()
{
    DocumentCommands commands;
    ShellServices shellServices;
    shellServices.cmdCatalog = &commands;
    shellServices.cmdInvoker = &commands;
    shellServices.cmdState = &commands;

    ApiPtr<IShellRuntime> runtime(createShellRuntime(shellServices));
    QVERIFY(runtime);
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("unknown command 'unknown.command'")));
    QVERIFY2(runtime->loadFromJson(commandProfileJson(defaultToolbarCommands())), qPrintable(runtime->lastError()));

    QWidget* shell = runtime->shell();
    const QList<QAction*> toolbar = toolbarActions(shell);
    QCOMPARE(toolbar.size(), 3);  // unknown.command 被跳过
    QCOMPARE(toolbar.at(0), menuAction(shell, QStringLiteral("file.new")));
    QCOMPARE(toolbar.at(1), menuAction(shell, QStringLiteral("file.save")));
    QCOMPARE(toolbar.at(2), menuAction(shell, QStringLiteral("view.grid")));
    QCOMPARE(toolbar.at(0), runtime->commandAction(QStringLiteral("file.new")));
    QVERIFY(runtime->commandAction(QStringLiteral("unknown.command")) == nullptr);

    // 快捷键来自命令表，动作挂在 shell 上，整个窗口都能触发；提示里带上快捷键。
    QAction* newAction = toolbar.at(0);
    QCOMPARE(newAction->shortcut(), QKeySequence(QStringLiteral("Ctrl+N")));
    QVERIFY(shell->actions().contains(newAction));
    QVERIFY2(newAction->toolTip().contains(newAction->shortcut().toString(QKeySequence::NativeText)),
        qPrintable(newAction->toolTip()));

    // 点工具栏按钮：命令只执行一次（菜单与工具栏没有各连一次）。
    auto* toolBar = shell->findChild<QToolBar*>(QStringLiteral("panel:toolbar"));
    QVERIFY(toolBar != nullptr);
    auto* button = qobject_cast<QToolButton*>(toolBar->widgetForAction(newAction));
    QVERIFY(button != nullptr);
    button->click();
    QCOMPARE(commands.calls, QStringList{ QStringLiteral("file.new") });
}

// 状态只算一次：执行命令后自动刷新，外部变化经 refreshCommands()；菜单与工具栏同时变化。
void InteractionTest::commandStateSyncsAllEntries()
{
    DocumentCommands commands;
    ShellServices shellServices;
    shellServices.cmdCatalog = &commands;
    shellServices.cmdInvoker = &commands;
    shellServices.cmdState = &commands;

    ApiPtr<IShellRuntime> runtime(createShellRuntime(shellServices));
    QVERIFY(runtime);
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("unknown command")));
    QVERIFY2(runtime->loadFromJson(commandProfileJson(defaultToolbarCommands())), qPrintable(runtime->lastError()));

    QWidget* shell = runtime->shell();
    QAction* save = menuAction(shell, QStringLiteral("file.save"));
    QAction* grid = menuAction(shell, QStringLiteral("view.grid"));
    QVERIFY(save != nullptr);
    QVERIFY(grid != nullptr);

    QVERIFY(!save->isEnabled());  // 还没有文档
    menuAction(shell, QStringLiteral("file.new"))->trigger();
    QVERIFY(save->isEnabled());   // 「新建」之后自动刷新

    QVERIFY(grid->isCheckable());
    QVERIFY(!grid->isChecked());
    grid->trigger();
    QVERIFY(commands.grid);
    QVERIFY(grid->isChecked());

    // 状态在命令之外变化：面板 / 宿主调用 refreshCommands()。
    commands.hasDocument = false;
    commands.grid = false;
    runtime->refreshCommands();
    QVERIFY(!save->isEnabled());
    QVERIFY(!grid->isChecked());
}

// 切皮肤 / 切语言只重建菜单，不重建面板：新菜单仍然引用工具栏正在用的那一个动作。
void InteractionTest::sharedActionsSurviveShellRefresh()
{
    DocumentCommands commands;
    ShellServices shellServices;
    shellServices.cmdCatalog = &commands;
    shellServices.cmdInvoker = &commands;
    shellServices.cmdState = &commands;

    ApiPtr<IShellRuntime> runtime(createShellRuntime(shellServices));
    QVERIFY(runtime);
    const QJsonArray toolbarCommands{ QStringLiteral("file.new"), QStringLiteral("file.save") };
    QVERIFY2(runtime->loadFromJson(commandProfileJson(toolbarCommands)), qPrintable(runtime->lastError()));

    QAction* before = runtime->commandAction(QStringLiteral("file.new"));
    QVERIFY(before != nullptr);

    runtime->switchTheme(QString::fromLatin1(ThemeId::Dark));
    QWidget* shell = runtime->shell();
    QCOMPARE(menuAction(shell, QStringLiteral("file.new")), before);
    QCOMPARE(toolbarActions(shell).value(0), before);

    runtime->switchTheme(QString::fromLatin1(ThemeId::Light));
}

// 面板带参数调用：不弹代码绑定表绑定的对话框，参数原样交给命令；不带参数的 invoke 仍先弹对话框。
void InteractionTest::panelInvokeWithArgsSkipsDialog()
{
    class RecordingFactory final : public IDialogFactory
    {
    public:
        DialogResult openDialog(QWidget*, const QJsonObject&) override
        {
            ++called;
            return DialogResult{};  // Cancelled：命令不会执行
        }
        int called = 0;
    } factory;

    ApiPtr<IDialogFactoryRegistry> dialogs(createDialogRegistry());
    QVERIFY(dialogs);
    dialogs->registerFactory(QStringLiteral("dialog.params"), &factory);

    DocumentCommands commands;
    ShellServices shellServices;
    shellServices.cmdCatalog = &commands;
    shellServices.cmdInvoker = &commands;
    shellServices.cmdState = &commands;
    shellServices.dlgReg = dialogs.get();
    shellServices.dialogBindings.append(DialogCmdBinding{
        QStringLiteral("file.new"), QStringLiteral("dialog.params") });

    ApiPtr<IShellRuntime> runtime(createShellRuntime(shellServices));
    QVERIFY(runtime);
    QVERIFY2(runtime->loadFromJson(commandProfileJson(QJsonArray{ QStringLiteral("file.new") })),
        qPrintable(runtime->lastError()));

    IShellHost* host = runtime.get();
    host->invokeWithArgs(QStringLiteral("file.new"), QJsonObject{ { QStringLiteral("target"), 9 } });
    QCOMPARE(factory.called, 0);
    QCOMPARE(commands.calls, QStringList{ QStringLiteral("file.new") });
    QCOMPARE(commands.lastArgs.value(QStringLiteral("target")).toInt(), 9);
    QVERIFY(runtime->commandAction(QStringLiteral("file.save"))->isEnabled());  // 带参执行后同样刷新状态

    host->invoke(QStringLiteral("file.new"));
    QCOMPARE(factory.called, 1);
    QCOMPARE(commands.calls.size(), 1);  // 对话框被取消，命令没有再执行
}

// 工具栏 props 写错时进入装配期诊断，不创建控件。
void InteractionTest::toolbarPropsAreValidated()
{
    DocumentCommands commands;
    ShellServices shellServices;
    shellServices.cmdCatalog = &commands;
    shellServices.cmdInvoker = &commands;

    ApiPtr<IShellRuntime> runtime(createShellRuntime(shellServices));
    QVERIFY(runtime);

    // commands 不是字符串数组
    runtime->loadFromJson(commandProfileJson(QJsonArray{ 1, 2 }));
    QVERIFY(runtime->shell()->findChild<QToolBar*>(QStringLiteral("panel:toolbar")) == nullptr);

    bool reported = false;
    for (const Diagnostic& diagnostic : runtime->diagnostics())
    {
        if (diagnostic.strMsg.contains(QStringLiteral("toolbar 'commands'")))
            reported = true;
    }
    QVERIFY(reported);
}

// 品牌系列由客户模块声明，shell 打开任何对话框时作为 DialogParam::Series 传给工厂；显式 params 优先。
void InteractionTest::dialogParamsCarryStyleSeries()
{
    class RecordingSeriesFactory final : public IDialogFactory
    {
    public:
        DialogResult openDialog(QWidget*, const QJsonObject& params) override
        {
            series = params.value(QString::fromLatin1(DialogParam::Series)).toString();
            return DialogResult{};
        }

        QString series;
    } factory;

    ApiPtr<IDialogFactoryRegistry> dialogs(createDialogRegistry());
    QVERIFY(dialogs);
    dialogs->registerFactory(QStringLiteral("dialog.series"), &factory);

    ShellServices shellServices;
    shellServices.dlgReg = dialogs.get();
    shellServices.strStyleSeries = QStringLiteral("b");
    ApiPtr<IShellRuntime> runtime(createShellRuntime(shellServices));
    QVERIFY(runtime);
    QVERIFY2(runtime->loadFromJson(test::minimalProfileJson(QStringLiteral("interaction.series"))),
        qPrintable(runtime->lastError()));

    runtime->openDialog(QStringLiteral("dialog.series"), nullptr);
    QCOMPARE(factory.series, QStringLiteral("b"));

    runtime->openDialog(QStringLiteral("dialog.series"), nullptr,
        QJsonObject{ { QString::fromLatin1(DialogParam::Series), QStringLiteral("pinned") } });
    QCOMPARE(factory.series, QStringLiteral("pinned"));
}

// 两套皮肤都覆盖代码里用到的控件与角色：新加的界面（输入框、滑块、工具栏、对话框品牌色）不会只在一套皮肤里有样式。
void InteractionTest::skinCoversSharedWidgets()
{
    // 先建一个运行期：静态链接下由它保留 shell 的 qrc 资源。
    ShellServices shellServices;
    ApiPtr<IShellRuntime> runtime(createShellRuntime(shellServices));
    QVERIFY(runtime);

    const QStringList required{
        QStringLiteral("QSpinBox"),
        QStringLiteral("QDoubleSpinBox"),
        QStringLiteral("QLineEdit"),
        QStringLiteral("QSlider::handle:horizontal"),
        QStringLiteral("QPlainTextEdit"),
        QStringLiteral("QToolTip"),
        QStringLiteral("QToolBar[msuiRole=\"toolbar\"]"),
        QStringLiteral("QToolBar QToolButton:checked"),
        QStringLiteral("QDialog[msuiRole=\"dialog\"][msuiSeries=\"mantisoft\"]"),
        QStringLiteral("QDialog[msuiRole=\"dialog\"][msuiSeries=\"tyvokstudio\"]"),
        QStringLiteral("QDialog[msuiRole=\"dialog\"][msuiSeries=\"goodlaser\"]"),
        QStringLiteral("QFrame[msuiRole=\"panel\"][msuiSeries=\"mantisoft\"] QToolButton"),
    };

    for (const QString& themeId : { QString::fromLatin1(ThemeId::Light), QString::fromLatin1(ThemeId::Dark) })
    {
        QFile file(QStringLiteral(":/UiShell/styles/") + themeId + QStringLiteral(".qss"));
        QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.fileName()));
        const QString qss = QString::fromUtf8(file.readAll());

        for (const QString& selector : required)
            QVERIFY2(qss.contains(selector), qPrintable(themeId + QStringLiteral(": missing ") + selector));

        // 旧版 GoodLaser Widgets 关于框已删除，它的横幅规则不应再留在皮肤里。
        QVERIFY2(!qss.contains(QStringLiteral("aboutBanner")), qPrintable(themeId));
    }
}

QTEST_MAIN(InteractionTest)

#include "tstInteraction.moc"
