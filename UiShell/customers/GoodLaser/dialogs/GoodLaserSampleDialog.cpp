#include "GoodLaserSampleDialog.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDialog>
#include <QKeySequence>
#include <QVBoxLayout>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWidget>
#include <QShortcut>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>
#include <QtMath>

#include "UiDialogs/SampleDialogs.h"
#include "UiShell/Contract/ModalDialog.h"

namespace GL
{
    namespace
    {
        const char* const kSampleQmlSource = "qrc:/GoodLaser/SampleDialog.qml";
    }  // namespace

    RD::DialogResult GoodLaserSampleDialogFactory::openDialog(QWidget* parent, const QJsonObject& params)
    {
        RD::samples::SampleParams values = RD::samples::SampleParams::fromJson(params);
        values.method = QStringLiteral("newton");

        auto* dialog = RD::markDialog(new QDialog(parent), params);  // 品牌色来自 params 里的系列
        dialog->setObjectName(QStringLiteral("GoodLaser.sample.dialog"));
        dialog->setWindowTitle(QCoreApplication::translate("GoodLaserSample", "Sample algorithm (Good Laser)"));
        dialog->setModal(true);
        dialog->setMinimumWidth(380);

        auto* layout = new QVBoxLayout(dialog);
        layout->setContentsMargins(0, 0, 0, 0);


        // 1. 初值 QVariantMap
        QVariantMap msParams;
        msParams.insert(QStringLiteral("target"), values.target);
        msParams.insert(QStringLiteral("maxIterations"), values.maxIterations);
        msParams.insert(QStringLiteral("tolerance"), values.tolerance);
        msParams.insert(QStringLiteral("method"), values.method);

        // 2. 翻译
        QVariantMap msLabels;
        msLabels.insert(QStringLiteral("windowTitle"),
            dialog->windowTitle());

        msLabels.insert(QStringLiteral("target"),
            QCoreApplication::translate("GoodLaserSample", "Target value"));

        msLabels.insert(QStringLiteral("iterations"),
            QCoreApplication::translate("GoodLaserSample", "Iterations"));

        msLabels.insert(QStringLiteral("methodFixed"),
            QCoreApplication::translate("GoodLaserSample", "Method: Newton (fixed), tolerance %1"));

        msLabels.insert(QStringLiteral("runButton"),
            QCoreApplication::translate("GoodLaserSample", "Run"));

        msLabels.insert(QStringLiteral("cancelButton"),
            QCoreApplication::translate("GoodLaserSample", "Cancel"));

        // 3. 主题
        const QString themeId = params.value(QStringLiteral("theme")).toString();

        // 创建 QML 面板
        auto* quick = new QQuickWidget(dialog);
        quick->setObjectName(QStringLiteral("GoodLaser.sample.quick"));
        quick->setResizeMode(QQuickWidget::SizeRootObjectToView);

        QQmlContext* ctx = quick->rootContext();
        ctx->setContextProperty(QStringLiteral("msParams"), msParams);
        ctx->setContextProperty(QStringLiteral("msLabels"), msLabels);
        ctx->setContextProperty(QStringLiteral("msTheme"), themeId.isEmpty() ? QStringLiteral("light") : themeId);

        quick->setSource(QUrl(QString::fromLatin1(kSampleQmlSource)));
        layout->addWidget(quick);

        QQuickItem* root = quick->rootObject();
        if (quick->status() != QQuickWidget::Ready || !root)
        {
            QStringList errors;
            for (const QQmlError& error : quick->errors())
                errors.append(error.toString());

            qWarning().noquote() << "GoodLaser: Sample dialog QML failed to load:"
                << (errors.isEmpty() ? "QML root object was not created" : errors.join(QLatin1Char('\n')));

            return RD::DialogResult{};
        }

        // 尺寸
        quick->setMinimumSize(qCeil(root->implicitWidth()), qCeil(root->implicitHeight()));
        dialog->resize(qCeil(root->implicitWidth()), qCeil(root->implicitHeight()));

        // QML → C++
        const bool acceptOk = QObject::connect(root, SIGNAL(accepted()), dialog, SLOT(accept()));
        const bool rejectOk = QObject::connect(root, SIGNAL(rejected()), dialog, SLOT(reject()));
        if (!acceptOk || !rejectOk)
            qWarning("GoodLaser: Sample dialog QML is missing accepted() / rejected() signals");

        // Esc / 回车关闭
        const QKeySequence closeKeys[] = {
            QKeySequence(QKeySequence::Cancel),
            QKeySequence(Qt::Key_Return),
            QKeySequence(Qt::Key_Enter)
        };

        for (const QKeySequence& keys : closeKeys)
        {
            auto* shortcut = new QShortcut(keys, dialog);
            QObject::connect(shortcut, &QShortcut::activated, dialog, &QDialog::reject);
        }

        auto* rootGuard = root;
        return RD::runModalDialog(dialog, [values, rootGuard](QDialog*) {
            RD::samples::SampleParams collected = values;

            const QVariant targetVar = rootGuard->property("targetValue");
            const QVariant iterVar = rootGuard->property("iterationsValue");

            if (targetVar.isValid())
                collected.target = targetVar.toDouble();

            if (iterVar.isValid())
                collected.maxIterations = iterVar.toInt();

            return collected.toJson();
            });
    }
}  // namespace GL