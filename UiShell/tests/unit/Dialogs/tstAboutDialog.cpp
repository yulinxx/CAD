#include <QtTest/QtTest>

#include <QApplication>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QJsonObject>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>

#include "AboutDialog.h"  // UiDialogs 内部头（dialogs/src）
#include "UiDialogs/AboutDialogs.h"

using namespace AA;
using namespace AA::about;

namespace
{
    QString labelText(const QWidget* root, const QString& objName)
    {
        const QLabel* label = root->findChild<QLabel*>(objName);
        return label ? label->text() : QString();
    }

    bool hasAnyEmptyLabel(const QWidget* root)
    {
        for (const QLabel* label : root->findChildren<QLabel*>())
        {
            // logo 行只有图片没有文字，不算空行。
            if (label->objectName() != QLatin1String("about.logo") && label->text().trimmed().isEmpty())
                return true;
        }
        return false;
    }

    QJsonObject sampleParams()
    {
        return QJsonObject{ { QStringLiteral("title"), QStringLiteral("Custom About") },
            { QStringLiteral("product"), QStringLiteral("Product A") },
            { QStringLiteral("version"), QStringLiteral("1.0") } };
    }

    // 模态 exec() 会阻塞：在对话框自己的事件循环里找到它并关闭；没找到就稍后重试。
    void closeWhenShown(const QString& objectName, int attempts = 200)
    {
        QTimer::singleShot(10, qApp, [objectName, attempts]() {
            for (QWidget* widget : QApplication::topLevelWidgets())
            {
                if (widget->objectName() == objectName && widget->isVisible())
                {
                    widget->close();
                    return;
                }
            }
            if (attempts > 0)
                closeWhenShown(objectName, attempts - 1);
        });
    }
}  // namespace

// 共享默认关于框（UiDialogs）：按 params 的结构化键渲染，走普通 IDialogFactory，由客户模块注册。
class AboutDialogTest : public QObject
{
    Q_OBJECT

private slots:
    void rendersOnlyProvidedKeys();
    void defaultTitleWhenMissing();
    void doesNotReadApplicationVersion();
    void showsLogoFromPath();
    void unreadableLogoIsSkipped();
    void closeButtonIsPresent();
    void factoryReturnsRejectedOnClose();
    void markupIsShownAsPlainText();
    void versionFallsBackToAppVersion();
};

// 只渲染 params 里有值的键：未提供的字段不应出现任何控件。
void AboutDialogTest::rendersOnlyProvidedKeys()
{
    AboutDialog dialog(AboutDlgData::setData(sampleParams()));

    QCOMPARE(dialog.windowTitle(), QStringLiteral("Custom About"));
    QCOMPARE(labelText(&dialog, QStringLiteral("about.product")), QStringLiteral("Product A"));
    QCOMPARE(labelText(&dialog, QStringLiteral("about.version")), QStringLiteral("Version: 1.0"));

    QVERIFY(dialog.findChild<QLabel*>(QStringLiteral("about.company")) == nullptr);
    QVERIFY(dialog.findChild<QLabel*>(QStringLiteral("about.website")) == nullptr);
    QVERIFY(dialog.findChild<QLabel*>(QStringLiteral("about.license")) == nullptr);
    QVERIFY(!hasAnyEmptyLabel(&dialog));
}

void AboutDialogTest::defaultTitleWhenMissing()
{
    AboutDialog dialog(AboutDlgData::setData(QJsonObject{ { QStringLiteral("product"), QStringLiteral("Product A") } }));
    QCOMPARE(dialog.windowTitle(), QStringLiteral("About"));
}

// 版本回退属于 shell 生成 params 时的逻辑：对话框不得自行读取 applicationVersion。
void AboutDialogTest::doesNotReadApplicationVersion()
{
    const QString previous = QCoreApplication::applicationVersion();
    QCoreApplication::setApplicationVersion(QStringLiteral("9.9.9"));

    AboutDialog dialog(AboutDlgData::setData(QJsonObject{ { QStringLiteral("product"), QStringLiteral("Product A") } }));
    QVERIFY(dialog.findChild<QLabel*>(QStringLiteral("about.version")) == nullptr);

    QCoreApplication::setApplicationVersion(previous);
}

// logoPath 是 shell 解析好的绝对路径，对话框直接加载。
void AboutDialogTest::showsLogoFromPath()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.filePath(QStringLiteral("logo.png"));
    QPixmap logo(16, 16);
    logo.fill(Qt::red);
    QVERIFY(logo.save(path));

    QJsonObject params = sampleParams();
    params.insert(QStringLiteral("logoPath"), path);
    AboutDialog dialog(AboutDlgData::setData(params));
    QVERIFY(dialog.findChild<QLabel*>(QStringLiteral("about.logo")) != nullptr);
}

void AboutDialogTest::unreadableLogoIsSkipped()
{
    QJsonObject params = sampleParams();
    params.insert(QStringLiteral("logoPath"), QStringLiteral("/nonexistent/logo.png"));
    AboutDialog dialog(AboutDlgData::setData(params));
    QVERIFY(dialog.findChild<QLabel*>(QStringLiteral("about.logo")) == nullptr);
}

void AboutDialogTest::closeButtonIsPresent()
{
    AboutDialog dialog(AboutDlgData::setData(sampleParams()));
    auto* buttons = dialog.findChild<QDialogButtonBox*>(QStringLiteral("about.buttons"));
    QVERIFY(buttons != nullptr);
    QVERIFY(buttons->button(QDialogButtonBox::Close) != nullptr);
    QCOMPARE(buttons->button(QDialogButtonBox::Close)->text(), QStringLiteral("Close"));
}

// 工厂返回真实的关闭方式：关闭按钮 / 窗口关闭都是 Rejected，展示型对话框没有数据。
void AboutDialogTest::factoryReturnsRejectedOnClose()
{
    AboutDialogFactory factory;
    closeWhenShown(QStringLiteral("msui.about.dialog"));
    const DialogResult result = factory.openDialog(nullptr, sampleParams());

    QCOMPARE(int(result.status), int(DialogResult::Rejected));
    QVERIFY(result.data.isEmpty());
}

// 配置文本按纯文本显示：license / 产品名里的 '<' 不会被当成 HTML 渲染。
void AboutDialogTest::markupIsShownAsPlainText()
{
    QJsonObject params = sampleParams();
    params.insert(QStringLiteral("license"), QStringLiteral("<b>MIT</b>"));
    AboutDialog dialog(AboutDlgData::setData(params));

    const QLabel* license = dialog.findChild<QLabel*>(QStringLiteral("about.license"));
    QVERIFY(license != nullptr);
    QCOMPARE(license->textFormat(), Qt::PlainText);
    QCOMPARE(license->text(), QStringLiteral("License: <b>MIT</b>"));
}

// version 没写时用 shell 补进的 appVersion（DialogParam::AppVersion）；显式写的 version 优先。
void AboutDialogTest::versionFallsBackToAppVersion()
{
    const QJsonObject fallback{ { QStringLiteral("product"), QStringLiteral("Product A") },
        { QString::fromLatin1(DialogParam::AppVersion), QStringLiteral("9.9.9") } };
    AboutDialog fromApp(AboutDlgData::setData(fallback));
    QCOMPARE(labelText(&fromApp, QStringLiteral("about.version")), QStringLiteral("Version: 9.9.9"));

    QJsonObject pinned = fallback;
    pinned.insert(QStringLiteral("version"), QStringLiteral("1.0"));
    AboutDialog explicitVersion(AboutDlgData::setData(pinned));
    QCOMPARE(labelText(&explicitVersion, QStringLiteral("about.version")), QStringLiteral("Version: 1.0"));
}

QTEST_MAIN(AboutDialogTest)

#include "tstAboutDialog.moc"
