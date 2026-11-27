#include <QtTest/QtTest>

#include <QApplication>
#include <QDialog>
#include <QJsonObject>
#include <QKeySequence>
#include <QPixmap>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWidget>
#include <QRegularExpression>
#include <QShortcut>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>

#include <functional>
#include <memory>

#include "dialogs/AboutDialogFactory.h"
#include "UiDialogs/AboutDialogs.h"
#include "UiShell/Contract/HostContext.h"

using namespace RD;
namespace Param = about::Param;
using GL::AboutDialogFactory;

namespace
{
    QString dialogName()
    {
        return QStringLiteral("GoodLaser.about.dialog");
    }

    // QML 元素按 objectName 查找：沿 childItems() 递归，Repeater 生成的行也在里面。
    QQuickItem* findItem(QQuickItem* root, const QString& objectName)
    {
        if (!root)
            return nullptr;
        if (root->objectName() == objectName)
            return root;
        for (QQuickItem* child : root->childItems())
        {
            if (QQuickItem* found = findItem(child, objectName))
                return found;
        }
        return nullptr;
    }

    QQuickItem* item(QQuickItem* root, const char* suffix)
    {
        return findItem(root, QStringLiteral("GoodLaser.about.") + QString::fromLatin1(suffix));
    }

    QString textOf(QQuickItem* root, const char* suffix)
    {
        QQuickItem* found = item(root, suffix);
        return found ? found->property("text").toString() : QString();
    }

    QJsonObject sampleParams()
    {
        return QJsonObject{ { QString::fromLatin1(Param::Product), QStringLiteral("Product C") },
            { QString::fromLatin1(Param::Version), QStringLiteral("2.0.0") },
            { QString::fromLatin1(Param::Company), QStringLiteral("Company C") } };
    }

    struct Opened
    {
        DialogResult result;
        bool shown = false;
        bool deletedAfterReturn = false;
    };

    // 经工厂打开对话框；显示后把 QDialog 与 QML 根对象交给 inspect。
    // inspect 可以自己关闭对话框（验证关闭途径）；没关时这里补一个 reject，保证 exec() 返回。
    Opened openAndInspect(const QJsonObject& params, std::function<void(QDialog*, QQuickItem*)> inspect)
    {
        // 轮询状态放在共享对象里：工厂没有打开对话框（返回 Cancelled）时，
        // 剩余的定时器在本函数返回后仍可能触发，不能引用栈上变量。
        struct Poll
        {
            bool done = false;
            bool shown = false;
            QPointer<QDialog> dialog;
            std::function<void(QDialog*, QQuickItem*)> inspect;
            std::function<void(int)> next;
        };
        auto state = std::make_shared<Poll>();
        state->inspect = std::move(inspect);

        std::weak_ptr<Poll> weak = state;
        state->next = [weak](int attempts) {
            QTimer::singleShot(10, qApp, [weak, attempts]() {
                const std::shared_ptr<Poll> poll = weak.lock();
                if (!poll || poll->done)
                    return;

                for (QWidget* widget : QApplication::topLevelWidgets())
                {
                    auto* dialog = qobject_cast<QDialog*>(widget);
                    if (!dialog || dialog->objectName() != dialogName() || !dialog->isVisible())
                        continue;

                    poll->done = true;
                    poll->shown = true;
                    poll->dialog = dialog;
                    auto* quick = dialog->findChild<QQuickWidget*>(QStringLiteral("GoodLaser.about.quick"));
                    poll->inspect(dialog, quick ? quick->rootObject() : nullptr);
                    if (poll->dialog && poll->dialog->isVisible())
                        poll->dialog->reject();
                    return;
                }
                if (attempts > 0)
                    poll->next(attempts - 1);
            });
        };
        state->next(200);

        Opened opened;
        opened.result = AboutDialogFactory().openDialog(nullptr, params);
        state->done = true;
        opened.shown = state->shown;
        opened.deletedAfterReturn = state->dialog.isNull();
        return opened;
    }
}  // namespace

// GoodLaser 的关于框：外层 QDialog + QQuickWidget 加载 qml/AboutDialog.qml。
// 验证数据进 QML（只读 about 对象）、QML 出信号（closeRequested / linkActivated）、皮肤、Esc 与释放。
class GoodLaserAboutDialogTest : public QObject
{
    Q_OBJECT

private slots:
    void loadsQmlAndShowsProvidedFields();
    void customerRowsAreAlwaysShown();
    void textIsPlain();
    void lightThemeByDefault();
    void followsDarkTheme();
    void logoUsesLocalFileUrl();
    void missingLogoIsHidden();
    void qrcLogoUsesQrcUrl();
    void closeRequestedRejects();
    void escapeShortcutRejects();
    void unsafeLinkIsBlocked();
};

void GoodLaserAboutDialogTest::loadsQmlAndShowsProvidedFields()
{
    QString title;
    QString product;
    QString version;
    QString company;
    bool websiteShown = true;
    const Opened opened = openAndInspect(sampleParams(), [&](QDialog* dialog, QQuickItem* root) {
        QVERIFY(root != nullptr);
        title = dialog->windowTitle();
        product = textOf(root, "product");
        version = textOf(root, "version");
        company = textOf(root, "company");
        websiteShown = item(root, "website") != nullptr;
    });

    QVERIFY(opened.shown);
    QCOMPARE(title, QStringLiteral("About"));
    QCOMPARE(product, QStringLiteral("Product C"));
    QCOMPARE(version, QStringLiteral("Version 2.0.0"));
    QCOMPARE(company, QStringLiteral("Company C"));
    QVERIFY(!websiteShown);  // 没提供的字段不生成行
    QCOMPARE(int(opened.result.status), int(DialogResult::Rejected));
    QVERIFY(opened.deletedAfterReturn);
}

// GoodLaser 专属的两行与 params 无关，始终显示。
void GoodLaserAboutDialogTest::customerRowsAreAlwaysShown()
{
    QString diagnostics;
    QString channel;
    openAndInspect(QJsonObject(), [&](QDialog*, QQuickItem* root) {
        diagnostics = textOf(root, "diagnostics");
        channel = textOf(root, "channel");
    });
    QCOMPARE(diagnostics, QStringLiteral("Diagnostics: implemented by Good Laser"));
    QCOMPARE(channel, QStringLiteral("Build channel: internal preview"));
}

// QML 的 Text 一律 PlainText：配置里的 '<' 不会被当成富文本。
void GoodLaserAboutDialogTest::textIsPlain()
{
    QJsonObject params = sampleParams();
    params.insert(QString::fromLatin1(Param::Product), QStringLiteral("<b>Product</b>"));

    QString text;
    int format = -1;
    openAndInspect(params, [&](QDialog*, QQuickItem* root) {
        if (QQuickItem* product = item(root, "product"))
        {
            text = product->property("text").toString();
            format = product->property("textFormat").toInt();
        }
    });
    QCOMPARE(text, QStringLiteral("<b>Product</b>"));
    QCOMPARE(format, int(Qt::PlainText));
}

void GoodLaserAboutDialogTest::lightThemeByDefault()
{
    bool dark = true;
    openAndInspect(sampleParams(), [&](QDialog*, QQuickItem* root) {
        dark = root->property("dark").toBool();
    });
    QVERIFY(!dark);
}

// shell 打开对话框时补上 DialogParam::Theme；QML 据此切换深色配色。
void GoodLaserAboutDialogTest::followsDarkTheme()
{
    QJsonObject params = sampleParams();
    params.insert(QString::fromLatin1(DialogParam::Theme), QString::fromLatin1(ThemeId::Dark));

    bool dark = false;
    openAndInspect(params, [&](QDialog*, QQuickItem* root) {
        dark = root->property("dark").toBool();
    });
    QVERIFY(dark);
}

// logo 是本地绝对路径：工厂转成 file URL 交给 QML 的 Image，Windows 路径才能加载。
void GoodLaserAboutDialogTest::logoUsesLocalFileUrl()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("logo.png"));
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::darkYellow);
    QVERIFY(pixmap.save(path));

    QJsonObject params = sampleParams();
    params.insert(QString::fromLatin1(Param::LogoPath), path);

    QUrl source;
    bool visible = false;
    openAndInspect(params, [&](QDialog*, QQuickItem* root) {
        if (QQuickItem* logo = item(root, "logo"))
        {
            source = logo->property("source").toUrl();
            visible = logo->isVisible();
        }
    });
    QCOMPARE(source, QUrl::fromLocalFile(path));
    QVERIFY(visible);
}

void GoodLaserAboutDialogTest::missingLogoIsHidden()
{
    bool visible = true;
    openAndInspect(sampleParams(), [&](QDialog*, QQuickItem* root) {
        QQuickItem* logo = item(root, "logo");
        visible = logo && logo->isVisible();
    });
    QVERIFY(!visible);
}

// profile 内嵌在 qrc 里时 logoPath 是 ":/..."：要转成 qrc:/...，不能当本地文件。
void GoodLaserAboutDialogTest::qrcLogoUsesQrcUrl()
{
    QJsonObject params = sampleParams();
    params.insert(QString::fromLatin1(Param::LogoPath), QStringLiteral(":/GoodLaser/missing_logo.png"));

    QUrl source;
    openAndInspect(params, [&](QDialog*, QQuickItem* root) {
        if (QQuickItem* logo = item(root, "logo"))
            source = logo->property("source").toUrl();
    });
    QCOMPARE(source, QUrl(QStringLiteral("qrc:/GoodLaser/missing_logo.png")));
}

// QML 的「关闭」只发 closeRequested()：由 C++ 接到 QDialog::reject。
void GoodLaserAboutDialogTest::closeRequestedRejects()
{
    bool invoked = false;
    const Opened opened = openAndInspect(sampleParams(), [&](QDialog*, QQuickItem* root) {
        invoked = QMetaObject::invokeMethod(root, "closeRequested");
    });
    QVERIFY(invoked);
    QCOMPARE(int(opened.result.status), int(DialogResult::Rejected));
    QVERIFY(opened.deletedAfterReturn);
}

// Esc 用窗口级快捷键处理，不依赖按键能否从 QQuickWidget 传回 QDialog。
void GoodLaserAboutDialogTest::escapeShortcutRejects()
{
    bool found = false;
    bool stillVisible = true;
    openAndInspect(sampleParams(), [&](QDialog* dialog, QQuickItem*) {
        for (QShortcut* shortcut : dialog->findChildren<QShortcut*>())
        {
            if (shortcut->key() == QKeySequence(QKeySequence::Cancel))
            {
                found = true;
                QMetaObject::invokeMethod(shortcut, "activated");
                break;
            }
        }
        stillVisible = dialog->isVisible();
    });
    QVERIFY(found);
    QVERIFY(!stillVisible);
}

// 链接是否打开由 C++ 决定：非 http / https / mailto 被拦下并告警，对话框保持打开。
void GoodLaserAboutDialogTest::unsafeLinkIsBlocked()
{
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("blocked About dialog link")));

    bool invoked = false;
    bool stillVisible = false;
    openAndInspect(sampleParams(), [&](QDialog* dialog, QQuickItem* root) {
        invoked = QMetaObject::invokeMethod(root, "linkActivated",
            Q_ARG(QString, QStringLiteral("javascript:alert(1)")));
        stillVisible = dialog->isVisible();
    });
    QVERIFY(invoked);
    QVERIFY(stillVisible);
}

// 无显卡的 CI / offscreen 下用 Qt Quick 软件渲染；必须在 QApplication 之前设置。
int main(int argc, char** argv)
{
    if (qEnvironmentVariableIsEmpty("QT_QUICK_BACKEND"))
        qputenv("QT_QUICK_BACKEND", "software");

    QApplication app(argc, argv);
    GoodLaserAboutDialogTest test;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&test, argc, argv);
}

#include "tstGoodLaserAboutDialog.moc"
