#include "SampleParamsDialog.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QJsonArray>
#include <QJsonValue>
#include <QSpinBox>
#include <QVBoxLayout>

#include "UiDialogs/SampleDialogs.h"

namespace RD::samples
{
    SampleParamsDialog::SampleParamsDialog(const QJsonObject& params, QWidget* parent)
        : QDialog(parent)
    {
        const SampleParams initial = SampleParams::fromJson(params);

        setObjectName(QStringLiteral("sample.params.dialog"));
        setWindowTitle(QCoreApplication::translate("SampleDialogs", "Sample algorithm parameters"));
        setModal(true);
        setMinimumWidth(360);

        auto* layout = new QVBoxLayout(this);
        auto* form = new QFormLayout;
        layout->addLayout(form);

        // 先设精度再设范围和值：QDoubleSpinBox 会按精度取整。
        m_target = new QDoubleSpinBox(this);
        m_target->setObjectName(QStringLiteral("sample.target"));
        m_target->setDecimals(4);
        m_target->setRange(0.0, SampleLimits::kMaxTarget);
        m_target->setValue(initial.target);
        form->addRow(QCoreApplication::translate("SampleDialogs", "Target value"), m_target);

        m_maxIterations = new QSpinBox(this);
        m_maxIterations->setObjectName(QStringLiteral("sample.maxIterations"));
        m_maxIterations->setRange(SampleLimits::kMinIterations, SampleLimits::kMaxIterations);
        m_maxIterations->setValue(initial.maxIterations);
        form->addRow(QCoreApplication::translate("SampleDialogs", "Max iterations"), m_maxIterations);

        m_tolerance = new QDoubleSpinBox(this);
        m_tolerance->setObjectName(QStringLiteral("sample.tolerance"));
        m_tolerance->setDecimals(12);
        m_tolerance->setRange(SampleLimits::kMinTolerance, SampleLimits::kMaxTolerance);
        m_tolerance->setSingleStep(1e-6);
        m_tolerance->setValue(initial.tolerance);
        form->addRow(QCoreApplication::translate("SampleDialogs", "Tolerance"), m_tolerance);

        // methods 限制可选方法（客户差异写在绑定 params）；为空或全部无效时列出全部方法。
        QStringList allowed;
        const QJsonArray methods = params.value(QStringLiteral("methods")).toArray();
        for (const QJsonValue& value : methods)
        {
            const QString id = value.toString();
            if (sampleMethods().contains(id) && !allowed.contains(id))
                allowed.append(id);
        }
        if (allowed.isEmpty())
            allowed = sampleMethods();

        m_method = new QComboBox(this);
        m_method->setObjectName(QStringLiteral("sample.method"));
        for (const QString& id : allowed)
            m_method->addItem(methodDisplayName(id), id);
        const int index = m_method->findData(initial.method);
        m_method->setCurrentIndex(index >= 0 ? index : 0);
        form->addRow(QCoreApplication::translate("SampleDialogs", "Method"), m_method);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        buttons->setObjectName(QStringLiteral("sample.buttons"));
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        layout->addWidget(buttons);
    }

    SampleParams SampleParamsDialog::values() const
    {
        SampleParams params;
        params.target = m_target->value();
        params.maxIterations = m_maxIterations->value();
        params.tolerance = m_tolerance->value();
        params.method = m_method->currentData().toString();
        return params;
    }
}  // namespace RD::samples