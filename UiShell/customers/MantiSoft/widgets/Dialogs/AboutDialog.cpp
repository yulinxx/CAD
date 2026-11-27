#include "AboutDialog.h"

#include <QCoreApplication>
#include "UiShell/Contract/ModalDialog.h"
#include <QDialog>
#include <QDialogButtonBox>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

namespace MS
{
    namespace
    {
        constexpr int kLogoMaxSize = 64;  // logo 显示上限（像素）

        // 工厂自翻译的默认数据（取代原 ui.json dialogBindings 的 about params）。
        // 文本由工厂翻译；logoPath 仍由 params 传入（相对路径，由 resolveParams 按部署方式解析）。
        RD::about::AboutDlgData makeAboutData()
        {
            RD::about::AboutDlgData data;
            data.product = QCoreApplication::translate("about", "about.a.product");
            data.version = QStringLiteral("1.4.2");
            data.company = QCoreApplication::translate("about", "about.a.company");
            data.website = QStringLiteral("https://example.com/mantisoft");
            return data;
        }

        // 带标题的分隔线 "─ text ─────"
        QWidget* sectionTitle(QWidget* parent, const QString& text)
        {
            auto* box = new QWidget(parent);
            auto* lay = new QHBoxLayout(box);
            lay->setContentsMargins(0, 0, 0, 0);
            lay->setSpacing(8);

            auto* label = new QLabel(text, box);
            label->setStyleSheet(QStringLiteral(
                "font-weight: bold; color: #666; padding: 4px 0px;"));
            lay->addWidget(label);

            auto* line = new QFrame(box);
            line->setFrameShape(QFrame::HLine);
            line->setStyleSheet(QStringLiteral("color: #555;"));
            lay->addWidget(line, 1);
            return box;
        }

        // 加载 logo：支持 qrc（":/..."）或绝对路径；失败时回退到客户资源。
        QPixmap loadLogo(const QString& logoPath)
        {
            if (logoPath.isEmpty()) return {};
            QPixmap pm(logoPath);
            if (pm.isNull()) pm = QPixmap(":/MantiSoft/logo.svg");
            if (pm.isNull()) pm = QPixmap(":/MantiSoft/logo_a.png");
            if (pm.isNull()) pm = QPixmap(":/MantiSoft/assets/logo_a.png");
            return pm;
        }

        // 只缩小不放大；按设备像素比生成，高 DPI 缩放下不发虚。
        QPixmap scaledLogo(const QPixmap& source, qreal dpr)
        {
            const QSize limit = QSize(kLogoMaxSize, kLogoMaxSize) * dpr;
            if (source.width() <= limit.width() && source.height() <= limit.height())
                return source;

            QPixmap pixmap = source.scaled(limit, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            pixmap.setDevicePixelRatio(dpr);
            return pixmap;
        }

        // 纯文本值标签：可选中、不解释 HTML，避免配置内容被当成标记。
        QLabel* valueLabel(const QString& objectName, const QString& text, QWidget* parent)
        {
            auto* label = new QLabel(text, parent);
            label->setObjectName(objectName);
            label->setTextFormat(Qt::PlainText);
            label->setWordWrap(true);
            label->setTextInteractionFlags(Qt::TextSelectableByMouse);
            return label;
        }
    }  // namespace

    // —— AboutDialogFactory ——
    RD::DialogResult AboutDialogFactory::openDialog(QWidget* parent, const QJsonObject& params)
    {
        RD::about::AboutDlgData info = makeAboutData();
        info.applyParams(params);  // params 覆盖默认值（测试传参 / 宿主注入 logoPath）。

        auto* dialog = new QDialog(parent);
        dialog->setObjectName(QStringLiteral("MantiSoft.about.dialog"));
        dialog->setWindowTitle(info.title.isEmpty()
            ? QCoreApplication::translate("MantiSoftAbout", "About") : info.title);
        dialog->setMinimumWidth(420);

        auto* layout = new QVBoxLayout(dialog);
        layout->setContentsMargins(24, 20, 24, 16);
        layout->setSpacing(12);

        // 顶部：logo + 产品名 / 版本
        auto* header = new QHBoxLayout();
        header->setSpacing(16);
        const QPixmap logo = loadLogo(info.logoPath);
        if (!logo.isNull())
        {
            auto* logoLabel = new QLabel(dialog);
            logoLabel->setObjectName(QStringLiteral("MantiSoft.about.logo"));
            logoLabel->setPixmap(scaledLogo(logo, dialog->devicePixelRatioF()));
            header->addWidget(logoLabel, 0, Qt::AlignTop);
        }

        auto* titleColumn = new QVBoxLayout();
        titleColumn->setSpacing(4);
        if (!info.product.isEmpty())
        {
            auto* product = valueLabel(QStringLiteral("MantiSoft.about.product"), info.product, dialog);
            QFont font = product->font();
            font.setPointSize(font.pointSize() + 5);
            font.setBold(true);
            product->setFont(font);
            titleColumn->addWidget(product);
        }
        if (!info.version.isEmpty())
        {
            titleColumn->addWidget(valueLabel(QStringLiteral("MantiSoft.about.version"),
                QCoreApplication::translate("MantiSoftAbout", "Version %1").arg(info.version), dialog));
        }
        titleColumn->addStretch(1);
        header->addLayout(titleColumn, 1);
        layout->addLayout(header);

        layout->addWidget(sectionTitle(dialog,
            QCoreApplication::translate("MantiSoftAbout", "Information")));

        auto* form = new QFormLayout();
        form->setHorizontalSpacing(12);
        form->setVerticalSpacing(6);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        if (!info.company.isEmpty())
            form->addRow(QCoreApplication::translate("MantiSoftAbout", "Company"),
                valueLabel(QStringLiteral("MantiSoft.about.company"), info.company, dialog));
        if (!info.website.isEmpty())
            form->addRow(QCoreApplication::translate("MantiSoftAbout", "Website"),
                valueLabel(QStringLiteral("MantiSoft.about.website"), info.website, dialog));
        if (form->rowCount() > 0)
            layout->addLayout(form);
        else
            delete form;

        auto* buttons = new QDialogButtonBox(dialog);
        auto* closeButton = buttons->addButton(QDialogButtonBox::Close);
        closeButton->setText(QCoreApplication::translate("MantiSoftAbout", "Close"));
        closeButton->setDefault(true);  // 回车与 Esc 都关闭。
        QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
        layout->addWidget(buttons);

        // markDialog 打上 msuiRole / msuiSeries，让框架 QSS rule 生效；
        // runModalDialog 负责 exec() 与 DialogResult 封装。展示型对话框只回传关闭方式，没有数据。
        return RD::runModalDialog(RD::markDialog(dialog, params));
    }
}  // namespace MS