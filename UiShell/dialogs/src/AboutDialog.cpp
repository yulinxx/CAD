#include "AboutDialog.h"

#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFont>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QSize>
#include <QVBoxLayout>

namespace AA::about
{
    namespace
    {
        // 信息行：params 键、已登记翻译的标签、AboutDlgData 里的字段。顺序即显示顺序。
        struct DisplayInfo
        {
            const char* key;
            const char* label;
            QString AboutDlgData::* field;
        };

        const DisplayInfo kInfo[] = {
            { Param::Version, QT_TRANSLATE_NOOP("AboutDialog", "Version"), &AboutDlgData::version },
            { Param::VersionDate, QT_TRANSLATE_NOOP("AboutDialog", "Version date"), &AboutDlgData::versionDate },
            { Param::Copyright, QT_TRANSLATE_NOOP("AboutDialog", "Copyright"), &AboutDlgData::copyright },
            { Param::Company, QT_TRANSLATE_NOOP("AboutDialog", "Company"), &AboutDlgData::company },
            { Param::Website, QT_TRANSLATE_NOOP("AboutDialog", "Website"), &AboutDlgData::website },
            { Param::Email, QT_TRANSLATE_NOOP("AboutDialog", "Email"), &AboutDlgData::email },
            { Param::License, QT_TRANSLATE_NOOP("AboutDialog", "License"), &AboutDlgData::license },
        };
    }  // namespace

    AboutDialog::AboutDialog(const AboutDlgData& info, QWidget* parent)
        : QDialog(parent)
    {
        setObjectName(QStringLiteral("msui.about.dialog"));
        setWindowTitle(info.title.isEmpty() ? QCoreApplication::translate("AboutDialog", "About") : info.title);
        // 模态由工厂里的 exec() 决定（见 runModalDialog），这里不再 setModal。
        setMinimumWidth(420);

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(24, 20, 24, 16);
        layout->setSpacing(10);

        if (!info.logoPath.isEmpty())
        {
            const QPixmap pixmap(info.logoPath);
            if (!pixmap.isNull())
            {
                auto* logo = new QLabel(this);
                logo->setObjectName(QStringLiteral("about.logo"));
                logo->setPixmap(pixmap.scaled(QSize(220, 72), Qt::KeepAspectRatio, Qt::SmoothTransformation));
                logo->setAlignment(Qt::AlignCenter);
                layout->addWidget(logo, 0, Qt::AlignHCenter);
            }
        }

        if (!info.product.isEmpty())
        {
            auto* product = new QLabel(info.product, this);
            product->setObjectName(QStringLiteral("about.product"));
            product->setTextFormat(Qt::PlainText);
            product->setAlignment(Qt::AlignCenter);
            product->setWordWrap(true);

            QFont productFont = product->font();
            productFont.setPointSize(productFont.pointSize() + 4);
            productFont.setBold(true);
            product->setFont(productFont);
            layout->addWidget(product);
        }

        for (const DisplayInfo& i : kInfo)
        {
            const QString& value = info.*(i.field);
            if (value.isEmpty())
                continue;

            const QString label = QCoreApplication::translate("AboutDialog", i.label);
            auto* line = new QLabel(QCoreApplication::translate("AboutDialog", "%1: %2").arg(label, value), this);
            line->setObjectName(QStringLiteral("about.") + QString::fromLatin1(i.key));
            line->setTextFormat(Qt::PlainText);
            line->setWordWrap(true);
            line->setTextInteractionFlags(Qt::TextSelectableByMouse);
            layout->addWidget(line);
        }

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
        buttons->setObjectName(QStringLiteral("about.buttons"));
        if (QPushButton* closeButton = buttons->button(QDialogButtonBox::Close))
        {
            closeButton->setText(QCoreApplication::translate("AboutDialog", "Close"));
            closeButton->setDefault(true);  // 回车与 Esc 都关闭。
        }
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        layout->addWidget(buttons);
    }
}  // namespace AA::about