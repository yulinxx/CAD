#include "AboutDialog.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSize>
#include <QStringList>
#include <QUrl>
#include <QVBoxLayout>

namespace TS
{
    namespace
    {
        constexpr int kLogoMaxSize = 64;  // logo 显示上限（像素）

        enum class RowKind
        {
            Text,
            Web,
            Mail
        };

        // 信息表的一行：params 键（也用作 objectName 后缀）、已登记翻译的标签、AA::about::AboutDlgData 字段、显示方式。
        struct DetailRow
        {
            const char* key;
            const char* label;
            QString AA::about::AboutDlgData::* field;
            RowKind kind;
        };

        const DetailRow kDetailRows[] = {
            { AA::about::Param::Company, QT_TRANSLATE_NOOP("TyvokStudioAbout", "Company"), &AA::about::AboutDlgData::company, RowKind::Text },
            { AA::about::Param::Copyright, QT_TRANSLATE_NOOP("TyvokStudioAbout", "Copyright"), &AA::about::AboutDlgData::copyright, RowKind::Text },
            { AA::about::Param::Website, QT_TRANSLATE_NOOP("TyvokStudioAbout", "Website"), &AA::about::AboutDlgData::website, RowKind::Web },
            { AA::about::Param::Email, QT_TRANSLATE_NOOP("TyvokStudioAbout", "Email"), &AA::about::AboutDlgData::email, RowKind::Mail },
        };

        QString objectNameFor(const char* key)
        {
            return QStringLiteral("TyvokStudio.about.") + QString::fromLatin1(key);
        }

        // 版本行：版本号与日期按有无组合成一句。
        QString versionLine(const AA::about::AboutDlgData& info)
        {
            if (!info.version.isEmpty() && !info.versionDate.isEmpty())
                return QCoreApplication::translate("TyvokStudioAbout", "Version %1 (%2)").arg(info.version, info.versionDate);
            if (!info.version.isEmpty())
                return QCoreApplication::translate("TyvokStudioAbout", "Version %1").arg(info.version);
            if (!info.versionDate.isEmpty())
                return QCoreApplication::translate("TyvokStudioAbout", "Released %1").arg(info.versionDate);
            return QString();
        }

        // 只缩小不放大；按设备像素比生成，150% / 200% 缩放下不发虚。
        QPixmap scaledLogo(const QPixmap& source, qreal dpr)
        {
            const QSize limit = QSize(kLogoMaxSize, kLogoMaxSize) * dpr;
            if (source.width() <= limit.width() && source.height() <= limit.height())
                return source;

            QPixmap pixmap = source.scaled(limit, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            pixmap.setDevicePixelRatio(dpr);
            return pixmap;
        }

        // 网址：缺少协议时按 https 处理；只放行 http / https，其它协议（file: / javascript: …）不生成链接。
        QUrl websiteUrl(const QString& text)
        {
            QUrl url(text, QUrl::StrictMode);
            if (url.scheme().isEmpty())
                url = QUrl(QStringLiteral("https://") + text, QUrl::StrictMode);

            const QString scheme = url.scheme().toLower();
            const bool allowed = scheme == QLatin1String("http") || scheme == QLatin1String("https");
            return (url.isValid() && allowed && !url.host().isEmpty()) ? url : QUrl();
        }

        // 邮箱：只接受 local@domain；不允许空白与 '?'，防止拼出带 subject / body 参数的 mailto。
        QUrl emailUrl(const QString& text)
        {
            const qsizetype at = text.indexOf(QLatin1Char('@'));
            const bool shapeOk = at > 0 && at == text.lastIndexOf(QLatin1Char('@')) && at < text.size() - 1;
            if (!shapeOk || text.contains(QLatin1Char(' ')) || text.contains(QLatin1Char('?')))
                return QUrl();

            const QUrl url(QStringLiteral("mailto:") + text, QUrl::StrictMode);
            return url.isValid() ? url : QUrl();
        }

        // 值标签：纯文本显示，配置里的 '<' 不会被当成 HTML。
        QLabel* textLabel(const QString& objectName, const QString& text, QWidget* parent)
        {
            auto* label = new QLabel(text, parent);
            label->setObjectName(objectName);
            label->setTextFormat(Qt::PlainText);
            label->setWordWrap(true);
            label->setTextInteractionFlags(Qt::TextSelectableByMouse);
            return label;
        }

        // 链接标签：href 来自校验过的 QUrl，显示文字转义；校验不过就退回纯文本。
        QLabel* linkLabel(const QString& objectName, const QString& text, const QUrl& url, QWidget* parent)
        {
            if (!url.isValid())
                return textLabel(objectName, text, parent);

            auto* label = new QLabel(parent);
            label->setObjectName(objectName);
            label->setTextFormat(Qt::RichText);
            label->setText(QStringLiteral("<a href=\"%1\">%2</a>")
                .arg(QString::fromUtf8(url.toEncoded()).toHtmlEscaped(), text.toHtmlEscaped()));
            label->setTextInteractionFlags(Qt::TextBrowserInteraction);
            label->setOpenExternalLinks(true);
            return label;
        }

        QLabel* valueWidget(const DetailRow& row, const QString& value, QWidget* parent)
        {
            const QString name = objectNameFor(row.key);
            switch (row.kind)
            {
            case RowKind::Web:
                return linkLabel(name, value, websiteUrl(value), parent);
            case RowKind::Mail:
                return linkLabel(name, value, emailUrl(value), parent);
            case RowKind::Text:
                break;
            }
            return textLabel(name, value, parent);
        }
    }  // namespace

    AboutDialog::AboutDialog(const AA::about::AboutDlgData& info, QWidget* parent)
        : QDialog(parent)
        , m_info(info)
    {
        setObjectName(QStringLiteral("TyvokStudio.about.dialog"));
        setWindowTitle(info.title.isEmpty() ? QCoreApplication::translate("TyvokStudioAbout", "About") : info.title);
        setMinimumWidth(480);
        // 模态由工厂里的 exec() 决定（见 runModalDialog），这里不 setModal。

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(24, 20, 24, 16);
        layout->setSpacing(12);

        // —— 顶部：logo + 产品名 / 版本 ——
        auto* header = new QHBoxLayout();
        header->setSpacing(16);

        if (!info.logoPath.isEmpty())
        {
            const QPixmap source(info.logoPath);
            if (!source.isNull())  // 读不到图片就不显示，不报错。
            {
                auto* logo = new QLabel(this);
                logo->setObjectName(QStringLiteral("TyvokStudio.about.logo"));
                logo->setPixmap(scaledLogo(source, devicePixelRatioF()));
                header->addWidget(logo, 0, Qt::AlignTop);
            }
        }

        auto* titleColumn = new QVBoxLayout();
        titleColumn->setSpacing(4);
        if (!info.product.isEmpty())
        {
            QLabel* product = textLabel(QStringLiteral("TyvokStudio.about.product"), info.product, this);
            QFont font = product->font();
            font.setPointSize(font.pointSize() + 5);
            font.setBold(true);
            product->setFont(font);
            titleColumn->addWidget(product);
        }
        const QString version = versionLine(info);
        if (!version.isEmpty())
            titleColumn->addWidget(textLabel(QStringLiteral("TyvokStudio.about.version"), version, this));
        titleColumn->addStretch(1);
        header->addLayout(titleColumn, 1);
        layout->addLayout(header);

        // —— 信息表：只放有值的行 ——
        auto* form = new QFormLayout();
        form->setHorizontalSpacing(12);
        form->setVerticalSpacing(6);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        for (const DetailRow& row : kDetailRows)
        {
            const QString& value = info.*(row.field);
            if (value.isEmpty())
                continue;
            form->addRow(QCoreApplication::translate("TyvokStudioAbout", row.label), valueWidget(row, value, this));
        }
        if (form->rowCount() > 0)
        {
            auto* separator = new QFrame(this);
            separator->setFrameShape(QFrame::HLine);
            separator->setFrameShadow(QFrame::Sunken);
            layout->addWidget(separator);
            layout->addLayout(form);
        }
        else
        {
            delete form;
        }

        // —— 许可声明：可能很长，放进可滚动的只读文本框，不把对话框撑高 ——
        if (!info.license.isEmpty())
        {
            layout->addWidget(textLabel(QStringLiteral("TyvokStudio.about.licenseCaption"),
                QCoreApplication::translate("TyvokStudioAbout", "License"), this));

            auto* license = new QPlainTextEdit(this);
            license->setObjectName(QStringLiteral("TyvokStudio.about.license"));
            license->setReadOnly(true);
            license->setPlainText(info.license);
            license->setTabChangesFocus(true);  // Tab 移到按钮，不被文本框吃掉
            license->setMaximumHeight(140);
            layout->addWidget(license);
        }

        // —— 按钮：复制信息 + 关闭 ——
        auto* buttons = new QDialogButtonBox(this);
        buttons->setObjectName(QStringLiteral("TyvokStudio.about.buttons"));

        QPushButton* copyButton = buttons->addButton(
            QCoreApplication::translate("TyvokStudioAbout", "Copy information"), QDialogButtonBox::ActionRole);
        copyButton->setObjectName(QStringLiteral("TyvokStudio.about.copy"));
        copyButton->setAutoDefault(false);
        connect(copyButton, &QPushButton::clicked, this, [this]() {
            if (QClipboard* clipboard = QGuiApplication::clipboard())
                clipboard->setText(plainText());
            });

        QPushButton* closeButton = buttons->addButton(QDialogButtonBox::Close);
        closeButton->setText(QCoreApplication::translate("TyvokStudioAbout", "Close"));
        closeButton->setDefault(true);  // 回车与 Esc 都关闭。
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        layout->addWidget(buttons);

        closeButton->setFocus();
    }

    QString AboutDialog::plainText() const
    {
        QStringList lines;
        if (!m_info.product.isEmpty())
            lines.append(m_info.product);

        const QString version = versionLine(m_info);
        if (!version.isEmpty())
            lines.append(version);

        for (const DetailRow& row : kDetailRows)
        {
            const QString& value = m_info.*(row.field);
            if (!value.isEmpty())
            {
                lines.append(QCoreApplication::translate("TyvokStudioAbout", "%1: %2")
                    .arg(QCoreApplication::translate("TyvokStudioAbout", row.label), value));
            }
        }
        return lines.join(QLatin1Char('\n'));
    }
}  // namespace TS