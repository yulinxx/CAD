#include "PanelFactory.h"

#include <QCoreApplication>
#include <QFrame>
#include <QLabel>
#include <QListWidget>
#include <QStringList>
#include <QVBoxLayout>

#include "UiShell/Contract/Retranslatable.h"

namespace
{
    using RetranslatableLabel = RD::Retranslatable<QLabel>;
    using RetranslatableFrame = RD::Retranslatable<QFrame>;
    using RetranslatableList = RD::Retranslatable<QListWidget>;

    // 状态栏面板：显示 IShellHost::showMessage() 推来的提示；语言切换时恢复默认文字。
    class StatusLabel final : public RD::Retranslatable<QLabel>, public RD::IStatusMessageView
    {
    public:
        explicit StatusLabel(QWidget* parent)
            : RD::Retranslatable<QLabel>(parent)
        {
        }

        void showStatusMessage(const QString& text) override
        {
            setText(text);
        }
    };

    QString panelTitle(const QString& panelId)
    {
        if (panelId == QLatin1String("top.brand"))
            return QCoreApplication::translate("TyvokStudio", "TyvokStudio · Project Workbench");
        if (panelId == QLatin1String("properties"))
            return QCoreApplication::translate("TyvokStudio", "Properties");
        if (panelId == QLatin1String("layers"))
            return QCoreApplication::translate("TyvokStudio", "Layers");
        if (panelId == QLatin1String("status"))
            return QCoreApplication::translate("TyvokStudio", "TyvokStudio Ready");

        return panelId;
    }

    QStringList texts(const QString& panelId)
    {
        if (panelId == QLatin1String("layers"))
        {
            return QStringList{
                QCoreApplication::translate("TyvokStudio", "Outline Layer"),
                QCoreApplication::translate("TyvokStudio", "Fill Layer"),
                QCoreApplication::translate("TyvokStudio", "Annotation Layer") };
        }

        return QStringList{
            QCoreApplication::translate("TyvokStudio", "Material:  Stainless Steel"),
            QCoreApplication::translate("TyvokStudio", "Thickness:  2.0 mm"),
            QCoreApplication::translate("TyvokStudio", "Speed:  1200 mm/min") };
    }
}  // namespace

namespace TS
{
    QWidget* PanelFactory::create(const RD::PanelCreateRequest& info)
    {
        if (info.strPanelId == QLatin1String("status"))
        {
            auto* label = new StatusLabel(info.parent);
            label->setObjectName(QStringLiteral("panel:") + info.strPanelId);
            label->setText(panelTitle(info.strPanelId));

            RD::markPanel(label, QStringLiteral("panelStatus"), QStringLiteral("tyvokstudio"));

            label->onRetranslate([label, panelId = info.strPanelId]() {
                label->setText(panelTitle(panelId));
                });

            return label;
        }

        // 顶部的「开始 / 暂停 / 示例算法」按钮已改由 profile 里的 msui.toolbar 声明（见 ui.json 的 top.actions），
        // 与菜单共用同一组命令动作，这里不再手写按钮。

        if (info.strPanelId == QLatin1String("properties") || info.strPanelId == QLatin1String("layers"))
        {
            auto* list = new RetranslatableList(info.parent);
            list->setObjectName(QStringLiteral("panel:") + info.strPanelId);
            RD::markPanel(list, QStringLiteral("panelList"), QStringLiteral("tyvokstudio"));
            list->addItems(texts(info.strPanelId));

            list->onRetranslate([list, panelId = info.strPanelId]() {
                const QStringList items = texts(panelId);
                for (int i = 0; i < list->count() && i < items.size(); ++i)
                    list->item(i)->setText(items.at(i));
                });
            return list;
        }

        auto* frame = new RetranslatableFrame(info.parent);
        frame->setObjectName(QStringLiteral("panel:") + info.strPanelId);
        RD::markPanel(frame, QStringLiteral("panel"), QStringLiteral("tyvokstudio"));
        auto* layout = new QVBoxLayout(frame);
        layout->setContentsMargins(10, 8, 10, 8);
        auto* title = new QLabel(panelTitle(info.strPanelId), frame);
        RD::markPanel(title, QStringLiteral("panelTitle"), QStringLiteral("tyvokstudio"));
        layout->addWidget(title);

        frame->onRetranslate([title, panelId = info.strPanelId]() {
            title->setText(panelTitle(panelId));
            });
        layout->addWidget(new QLabel(QStringLiteral("tyvokstudio / ") + info.strPanelId, frame));

        layout->addStretch(1);
        return frame;
    }

    // —— CenterView ——

    QWidget* CenterView::createCenterView(QWidget* parent)
    {
        auto* view = new RetranslatableLabel(parent);
        view->setText(QCoreApplication::translate(
            "TyvokStudio", "TyvokStudio Visualization View"));

        view->setObjectName(QStringLiteral("center.view"));
        view->setAlignment(Qt::AlignCenter);
        view->setMinimumSize(420, 300);
        RD::markPanel(view, QStringLiteral("centerView"), QStringLiteral("tyvokstudio"));

        view->onRetranslate([view]() {
            view->setText(QCoreApplication::translate(
                "TyvokStudio", "TyvokStudio Visualization View"));
            });
        return view;
    }
}  // namespace TS