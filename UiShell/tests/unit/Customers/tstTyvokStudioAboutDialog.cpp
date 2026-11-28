#include <QtTest/QtTest>

#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGuiApplication>
#include <QJsonObject>
#include <QLabel>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>

#include <functional>

#include "dialogs/AboutDialog.h"
#include "dialogs/AboutDialogFactory.h"
#include "UiDialogs/AboutDialogs.h"

using namespace AA;
using about::AboutDlgData;
namespace Param = about::Param;
using TS::AboutDialog;
using TS::AboutDialogFactory;

namespace
{
    QString name(const char* suffix)
    {
        return QStringLiteral("TyvokStudio.about.") + QString::fromLatin1(suffix);
    }

    const QLabel* label(const QWidget* root, const char* suffix)
    {
        return root->findChild<QLabel*>(name(suffix));
    }

    AboutDlgData sampleInfo()
    {
        AboutDlgData info;
        info.product = QStringLiteral("Product B");
        info.version = QStringLiteral("5.0.1");
        info.versionDate = QStringLiteral("2026-02-18");
        info.company = QStringLiteral("Company B");
        return info;
    }

    QPixmap labelPixmap(const QLabel* label)
    {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        return label->pixmap();
#else
        return label->pixmap(Qt::ReturnByValue);
#endif
    }

    // 写一张临时 png，返回路径；dir 由调用方持有，用例结束时删除。
    QString writeLogo(const QTemporaryDir& dir, int size)
    {
        const QString path = dir.filePath(QStringLiteral("logo_%1.png").arg(size));
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::darkGreen);
        return pixmap.save(path) ? path : QString();
    }

    // 模态 exec() 会阻塞：在对话框自己的事件循环里找到它，交给 action 处理；没找到就稍后重试。
    void whenShown(const QString& objectName, std::function<void(QDialog*)> action, int attempts = 200)
    {
        QTimer::singleShot(10, qApp, [objectName, action, attempts]() {
            for (QWidget* widget : QApplication::topLevelWidgets())
            {
                auto* dialog = qobject_cast<QDialog*>(widget);
                if (dialog && dialog->objectName() == objectName && dialog->isVisible())
                {
                    action(dialog);
                    return;
                }
            }
            if (attempts > 0)
                whenShown(objectName, action, attempts - 1);
        });
    }
}  // namespace

// TyvokStudio 的关于框：纯视图（AboutDlgData → 控件）+ 工厂（params → AboutDlgData → runModalDialog）。
class TyvokStudioAboutDialogTest : public QObject
{
    Q_OBJECT

private slots:
    void rendersHeaderAndVersionLine();
    void versionWithoutDate();
    void rendersOnlyProvidedRows();
    void defaultTitleWhenMissing();
    void markupIsShownAsPlainText();
    void websiteBecomesHttpsLink();
    void unsafeWebsiteStaysPlainText();
    void emailBecomesMailtoLink();
    void emailWithQueryStaysPlainText();
    void linkTextIsEscaped();
    void licenseIsReadOnlyAndBounded();
    void largeLogoIsScaledDown();
    void smallLogoIsNotUpscaled();
    void unreadableLogoIsSkipped();
    void closeIsDefaultButton();
    void copyButtonWritesPlainText();
    void factoryParsesParamsAndReturnsRejected();
};

void TyvokStudioAboutDialogTest::rendersHeaderAndVersionLine()
{
    AboutDialog dialog(sampleInfo());
    QCOMPARE(dialog.objectName(), name("dialog"));
    QCOMPARE(label(&dialog, "product")->text(), QStringLiteral("Product B"));
    QCOMPARE(label(&dialog, "version")->text(), QStringLiteral("Version 5.0.1 (2026-02-18)"));
}

void TyvokStudioAboutDialogTest::versionWithoutDate()
{
    AboutDlgData info = sampleInfo();
    info.versionDate.clear();
    AboutDialog dialog(info);
    QCOMPARE(label(&dialog, "version")->text(), QStringLiteral("Version 5.0.1"));
}

// 只显示有值的行：未提供的字段不出现任何控件。
void TyvokStudioAboutDialogTest::rendersOnlyProvidedRows()
{
    AboutDialog dialog(sampleInfo());
    QCOMPARE(label(&dialog, "company")->text(), QStringLiteral("Company B"));
    QVERIFY(label(&dialog, "copyright") == nullptr);
    QVERIFY(label(&dialog, "website") == nullptr);
    QVERIFY(label(&dialog, "email") == nullptr);
    QVERIFY(label(&dialog, "logo") == nullptr);
    QVERIFY(dialog.findChild<QPlainTextEdit*>(name("license")) == nullptr);
}

void TyvokStudioAboutDialogTest::defaultTitleWhenMissing()
{
    AboutDialog untitled(sampleInfo());
    QCOMPARE(untitled.windowTitle(), QStringLiteral("About"));

    AboutDlgData info = sampleInfo();
    info.title = QStringLiteral("About Product B");
    AboutDialog titled(info);
    QCOMPARE(titled.windowTitle(), QStringLiteral("About Product B"));
}

void TyvokStudioAboutDialogTest::markupIsShownAsPlainText()
{
    AboutDlgData info = sampleInfo();
    info.product = QStringLiteral("<b>Product</b>");
    AboutDialog dialog(info);

    const QLabel* product = label(&dialog, "product");
    QCOMPARE(product->textFormat(), Qt::PlainText);
    QCOMPARE(product->text(), QStringLiteral("<b>Product</b>"));
}

// 网址缺少协议时补 https，生成可点击链接。
void TyvokStudioAboutDialogTest::websiteBecomesHttpsLink()
{
    AboutDlgData info = sampleInfo();
    info.website = QStringLiteral("example.com/customer-b");
    AboutDialog dialog(info);

    const QLabel* website = label(&dialog, "website");
    QVERIFY(website != nullptr);
    QCOMPARE(website->textFormat(), Qt::RichText);
    QVERIFY(website->openExternalLinks());
    QVERIFY2(website->text().contains(QStringLiteral("href=\"https://example.com/customer-b\"")), qPrintable(website->text()));
}

// 只放行 http / https：javascript: / file: 不生成链接，按纯文本显示原值。
void TyvokStudioAboutDialogTest::unsafeWebsiteStaysPlainText()
{
    const QString values[] = { QStringLiteral("javascript:alert(1)"), QStringLiteral("file:///C:/Windows") };
    for (const QString& value : values)
    {
        AboutDlgData info = sampleInfo();
        info.website = value;
        AboutDialog dialog(info);

        const QLabel* website = label(&dialog, "website");
        QVERIFY(website != nullptr);
        QCOMPARE(website->textFormat(), Qt::PlainText);
        QCOMPARE(website->text(), value);
    }
}

void TyvokStudioAboutDialogTest::emailBecomesMailtoLink()
{
    AboutDlgData info = sampleInfo();
    info.email = QStringLiteral("support@customer-b.example");
    AboutDialog dialog(info);

    const QLabel* email = label(&dialog, "email");
    QVERIFY(email != nullptr);
    QCOMPARE(email->textFormat(), Qt::RichText);
    QVERIFY2(email->text().contains(QStringLiteral("href=\"mailto:support@customer-b.example\"")), qPrintable(email->text()));
}

// 带 '?' 的邮箱会拼出 subject / body 参数，按纯文本显示。
void TyvokStudioAboutDialogTest::emailWithQueryStaysPlainText()
{
    AboutDlgData info = sampleInfo();
    info.email = QStringLiteral("support@customer-b.example?subject=hi");
    AboutDialog dialog(info);

    const QLabel* email = label(&dialog, "email");
    QVERIFY(email != nullptr);
    QCOMPARE(email->textFormat(), Qt::PlainText);
}

// 链接标签是富文本：显示文字必须转义。
void TyvokStudioAboutDialogTest::linkTextIsEscaped()
{
    AboutDlgData info = sampleInfo();
    info.website = QStringLiteral("https://example.com/?a=1&b=2");
    AboutDialog dialog(info);

    const QLabel* website = label(&dialog, "website");
    QCOMPARE(website->textFormat(), Qt::RichText);
    QVERIFY2(website->text().contains(QStringLiteral(">https://example.com/?a=1&amp;b=2</a>")), qPrintable(website->text()));
}

// license 放在只读文本框里，并限制高度，不把对话框撑高。
void TyvokStudioAboutDialogTest::licenseIsReadOnlyAndBounded()
{
    AboutDlgData info = sampleInfo();
    info.license = QStringLiteral("Line 1\nLine 2\n<b>Line 3</b>");
    AboutDialog dialog(info);

    const auto* license = dialog.findChild<QPlainTextEdit*>(name("license"));
    QVERIFY(license != nullptr);
    QVERIFY(license->isReadOnly());
    QCOMPARE(license->toPlainText(), info.license);
    QVERIFY(license->maximumHeight() <= 140);
}

// 大图只缩小到 64 像素以内（按设备像素比生成）。
void TyvokStudioAboutDialogTest::largeLogoIsScaledDown()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    AboutDlgData info = sampleInfo();
    info.logoPath = writeLogo(dir, 300);
    QVERIFY(!info.logoPath.isEmpty());

    AboutDialog dialog(info);
    const QLabel* logo = label(&dialog, "logo");
    QVERIFY(logo != nullptr);
    const QPixmap pixmap = labelPixmap(logo);
    const QSizeF logical = QSizeF(pixmap.size()) / pixmap.devicePixelRatio();
    QVERIFY2(logical.width() <= 64.5 && logical.height() <= 64.5,
        qPrintable(QStringLiteral("%1x%2").arg(logical.width()).arg(logical.height())));
}

void TyvokStudioAboutDialogTest::smallLogoIsNotUpscaled()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    AboutDlgData info = sampleInfo();
    info.logoPath = writeLogo(dir, 16);

    AboutDialog dialog(info);
    const QLabel* logo = label(&dialog, "logo");
    QVERIFY(logo != nullptr);
    QCOMPARE(labelPixmap(logo).size(), QSize(16, 16));
}

void TyvokStudioAboutDialogTest::unreadableLogoIsSkipped()
{
    AboutDlgData info = sampleInfo();
    info.logoPath = QStringLiteral("/nonexistent/logo.png");
    AboutDialog dialog(info);
    QVERIFY(label(&dialog, "logo") == nullptr);
}

void TyvokStudioAboutDialogTest::closeIsDefaultButton()
{
    AboutDialog dialog(sampleInfo());
    const auto* buttons = dialog.findChild<QDialogButtonBox*>(name("buttons"));
    QVERIFY(buttons != nullptr);
    QPushButton* close = buttons->button(QDialogButtonBox::Close);
    QVERIFY(close != nullptr);
    QVERIFY(close->isDefault());
    QCOMPARE(close->text(), QStringLiteral("Close"));

    // 「复制信息」不能抢默认按钮，否则回车会复制而不是关闭。
    const auto* copy = dialog.findChild<QPushButton*>(name("copy"));
    QVERIFY(copy != nullptr);
    QVERIFY(!copy->isDefault());
    QVERIFY(!copy->autoDefault());
}

void TyvokStudioAboutDialogTest::copyButtonWritesPlainText()
{
    AboutDlgData info = sampleInfo();
    info.website = QStringLiteral("https://example.com/b");
    AboutDialog dialog(info);

    const QString expected = QStringLiteral(
        "Product B\nVersion 5.0.1 (2026-02-18)\nCompany: Company B\nWebsite: https://example.com/b");
    QCOMPARE(dialog.plainText(), expected);

    QClipboard* clipboard = QGuiApplication::clipboard();
    if (!clipboard)
        QSKIP("clipboard is not available on this platform plugin");
    clipboard->clear();

    auto* copy = dialog.findChild<QPushButton*>(name("copy"));
    QVERIFY(copy != nullptr);
    copy->click();
    QCOMPARE(clipboard->text(), expected);
}

// 工厂：读 params 的结构化键，模态打开，关闭后返回 Rejected、没有数据，并释放对话框。
void TyvokStudioAboutDialogTest::factoryParsesParamsAndReturnsRejected()
{
    const QJsonObject params{ { QString::fromLatin1(Param::Title), QStringLiteral("About B") },
        { QString::fromLatin1(Param::Product), QStringLiteral("Product B") },
        { QString::fromLatin1(Param::Version), QStringLiteral("5.0.1") } };

    QString seenTitle;
    QString seenProduct;
    QPointer<QDialog> seenDialog;
    whenShown(name("dialog"), [&](QDialog* dialog) {
        seenDialog = dialog;
        seenTitle = dialog->windowTitle();
        if (const QLabel* product = label(dialog, "product"))
            seenProduct = product->text();
        dialog->close();
    });

    AboutDialogFactory factory;
    const DialogResult result = factory.openDialog(nullptr, params);

    QCOMPARE(seenTitle, QStringLiteral("About B"));
    QCOMPARE(seenProduct, QStringLiteral("Product B"));
    QCOMPARE(int(result.status), int(DialogResult::Rejected));
    QVERIFY(result.data.isEmpty());
    QVERIFY(seenDialog.isNull());  // runModalDialog 返回前已释放
}

QTEST_MAIN(TyvokStudioAboutDialogTest)

#include "tstTyvokStudioAboutDialog.moc"
