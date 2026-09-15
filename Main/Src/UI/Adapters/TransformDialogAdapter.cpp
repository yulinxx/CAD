/**
 * @file TransformDialogAdapter.cpp
 * @brief TransformDialog 适配器实现
 */
#include "TransformDialogAdapter.h"
#include "SceneDocument2D.h"

#include <QCoreApplication>
#include <QDialog>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QLabel>
#include <QCheckBox>
#include <QComboBox>
#include <QRadioButton>
#include <QButtonGroup>
#include <QGroupBox>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QMessageBox>

namespace
{
    QString trTransform(const char* text)
    {
        return QCoreApplication::translate("TransformDialog", text);
    }
}  // namespace

TransformDialogAdapter::TransformDialogAdapter(SceneDocument2D* document, QWidget* parent)
    : m_document(document)
    , m_parent(parent)
{
}

TransformParameters TransformDialogAdapter::getParameters()
{
    auto dialog = createDialog();
    TransformParameters result;

    if (dialog->exec() == QDialog::Accepted)
    {
        result = collectParametersFromUI(dialog);
    }

    delete dialog;
    return result;
}

bool TransformDialogAdapter::hasValidParameters() const
{
    return validateParameters(m_parameters);
}

TransformParameters TransformDialogAdapter::currentParameters() const
{
    return m_parameters;
}

void TransformDialogAdapter::setParametersChangedCallback(std::function<void(const TransformParameters&)> callback)
{
    m_parametersChangedCallback = callback;
}

void TransformDialogAdapter::setConfirmedCallback(std::function<void(bool confirmed)> callback)
{
    m_confirmedCallback = callback;
}

void TransformDialogAdapter::cancel()
{
    m_parameters = TransformParameters();
    if (m_confirmedCallback)
    {
        m_confirmedCallback(false);
    }
}

TransformType TransformDialogAdapter::transformType() const
{
    return m_transformType;
}

void TransformDialogAdapter::setTransformType(TransformType type)
{
    m_transformType = type;
}

QString TransformDialogAdapter::getDialogTitle() const
{
    switch (m_transformType)
    {
    case TransformType::Move:
        return trTransform("Move");
    case TransformType::Copy:
        return trTransform("Copy");
    case TransformType::Rotate:
        return trTransform("Rotate");
    case TransformType::Mirror:
        return trTransform("Mirror");
    case TransformType::Scale:
        return trTransform("Scale");
    case TransformType::Shear:
        return trTransform("Shear");
    default:
        return trTransform("Transform");
    }
}

QDialog* TransformDialogAdapter::createDialog()
{
    auto dialog = new QDialog(m_parent);
    dialog->setWindowTitle(getDialogTitle());
    dialog->setMinimumWidth(300);

    auto layout = new QVBoxLayout(dialog);
    layout->setSpacing(12);
    layout->setContentsMargins(16, 16, 16, 16);

    // 根据变换类型添加输入控件
    switch (m_transformType)
    {
    case TransformType::Move:
        addMoveInputs(layout);
        break;
    case TransformType::Copy:
        addCopyInputs(layout);
        break;
    case TransformType::Rotate:
        addRotateInputs(layout);
        break;
    case TransformType::Mirror:
        addMirrorInputs(layout, dialog);
        break;
    default:
        break;
    }

    // 添加预览控件
    if (m_showPreview)
    {
        addPreviewControls(layout, dialog);
    }

    // 添加状态标签
    m_statusLabel = new QLabel(trTransform("Enter parameters"));
    layout->addWidget(m_statusLabel);

    // 添加按钮
    auto buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply);
    layout->addWidget(buttonBox);

    // 连接信号
    QObject::connect(buttonBox, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    QObject::connect(buttonBox, &QDialogButtonBox::rejected, dialog, &QDialog::reject);

    // 应用按钮
    if (auto applyButton = buttonBox->button(QDialogButtonBox::Apply))
    {
        QObject::connect(applyButton, &QPushButton::clicked, dialog, [this, dialog]() {
            auto params = collectParametersFromUI(dialog);
            if (validateParameters(params))
            {
                m_parameters = params;
                if (m_parametersChangedCallback)
                {
                    m_parametersChangedCallback(params);
                }
                m_statusLabel->setText(trTransform("Parameters applied"));
            }
            else
            {
                m_statusLabel->setText(trTransform("Invalid parameters"));
            }
        });
    }

    return dialog;
}

void TransformDialogAdapter::addMoveInputs(QVBoxLayout* layout)
{
    auto group = new QGroupBox(trTransform("Move Parameters"));
    auto groupLayout = new QFormLayout(group);
    groupLayout->setContentsMargins(12, 12, 12, 12);
    groupLayout->setSpacing(10);
    groupLayout->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    groupLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    m_moveXSpinBox = new QDoubleSpinBox();
    m_moveXSpinBox->setRange(-10000, 10000);
    m_moveXSpinBox->setSingleStep(0.1);
    m_moveXSpinBox->setValue(0);
    groupLayout->addRow("X:", m_moveXSpinBox);

    m_moveYSpinBox = new QDoubleSpinBox();
    m_moveYSpinBox->setRange(-10000, 10000);
    m_moveYSpinBox->setSingleStep(0.1);
    m_moveYSpinBox->setValue(0);
    groupLayout->addRow("Y:", m_moveYSpinBox);

    layout->addWidget(group);
}

void TransformDialogAdapter::addCopyInputs(QVBoxLayout* layout)
{
    auto group = new QGroupBox(trTransform("Copy Parameters"));
    auto groupLayout = new QFormLayout(group);
    groupLayout->setContentsMargins(12, 12, 12, 12);
    groupLayout->setSpacing(10);
    groupLayout->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    groupLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    m_copyCountSpinBox = new QSpinBox();
    m_copyCountSpinBox->setRange(1, 100);
    m_copyCountSpinBox->setValue(1);
    groupLayout->addRow(trTransform("Count:"), m_copyCountSpinBox);

    m_copySpacingXSpinBox = new QDoubleSpinBox();
    m_copySpacingXSpinBox->setRange(-10000, 10000);
    m_copySpacingXSpinBox->setSingleStep(0.1);
    m_copySpacingXSpinBox->setValue(0);
    groupLayout->addRow(trTransform("X Spacing:"), m_copySpacingXSpinBox);

    m_copySpacingYSpinBox = new QDoubleSpinBox();
    m_copySpacingYSpinBox->setRange(-10000, 10000);
    m_copySpacingYSpinBox->setSingleStep(0.1);
    m_copySpacingYSpinBox->setValue(0);
    groupLayout->addRow(trTransform("Y Spacing:"), m_copySpacingYSpinBox);

    layout->addWidget(group);
}

void TransformDialogAdapter::addRotateInputs(QVBoxLayout* layout)
{
    auto group = new QGroupBox(trTransform("Rotate Parameters"));
    auto groupLayout = new QFormLayout(group);
    groupLayout->setContentsMargins(12, 12, 12, 12);
    groupLayout->setSpacing(10);
    groupLayout->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    groupLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    m_rotateAngleSpinBox = new QDoubleSpinBox();
    m_rotateAngleSpinBox->setRange(-360, 360);
    m_rotateAngleSpinBox->setSingleStep(0.1);
    m_rotateAngleSpinBox->setValue(0);
    m_rotateAngleSpinBox->setSuffix("°");
    groupLayout->addRow(trTransform("Angle:"), m_rotateAngleSpinBox);

    m_rotateCenterXSpinBox = new QDoubleSpinBox();
    m_rotateCenterXSpinBox->setRange(-10000, 10000);
    m_rotateCenterXSpinBox->setSingleStep(0.1);
    m_rotateCenterXSpinBox->setValue(0);
    groupLayout->addRow(trTransform("Center X:"), m_rotateCenterXSpinBox);

    m_rotateCenterYSpinBox = new QDoubleSpinBox();
    m_rotateCenterYSpinBox->setRange(-10000, 10000);
    m_rotateCenterYSpinBox->setSingleStep(0.1);
    m_rotateCenterYSpinBox->setValue(0);
    groupLayout->addRow(trTransform("Center Y:"), m_rotateCenterYSpinBox);

    layout->addWidget(group);
}

void TransformDialogAdapter::addMirrorInputs(QVBoxLayout* layout, QDialog* dialog)
{
    auto group = new QGroupBox(trTransform("Mirror Parameters"));
    auto groupLayout = new QVBoxLayout(group);
    groupLayout->setContentsMargins(12, 12, 12, 12);
    groupLayout->setSpacing(10);

    // 镜像轴选择
    auto axisGroup = new QButtonGroup(group);

    auto xRadio = new QRadioButton(trTransform("X Axis"));
    axisGroup->addButton(xRadio, 0);
    groupLayout->addWidget(xRadio);

    auto yRadio = new QRadioButton(trTransform("Y Axis"));
    axisGroup->addButton(yRadio, 1);
    yRadio->setChecked(true);
    groupLayout->addWidget(yRadio);

    auto customRadio = new QRadioButton(trTransform("Custom Axis"));
    axisGroup->addButton(customRadio, 2);
    groupLayout->addWidget(customRadio);

    // 自定义轴输入（默认隐藏）
    auto customGroup = new QGroupBox(trTransform("Custom Axis Parameters"));
    auto customForm = new QFormLayout(customGroup);
    customForm->setContentsMargins(12, 12, 12, 12);
    customForm->setSpacing(10);
    customForm->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    customForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    m_mirrorLineX1SpinBox = new QDoubleSpinBox();
    m_mirrorLineX1SpinBox->setRange(-10000, 10000);
    m_mirrorLineX1SpinBox->setSingleStep(0.1);
    m_mirrorLineX1SpinBox->setValue(0);
    customForm->addRow(trTransform("Start X:"), m_mirrorLineX1SpinBox);

    m_mirrorLineY1SpinBox = new QDoubleSpinBox();
    m_mirrorLineY1SpinBox->setRange(-10000, 10000);
    m_mirrorLineY1SpinBox->setSingleStep(0.1);
    m_mirrorLineY1SpinBox->setValue(0);
    customForm->addRow(trTransform("Start Y:"), m_mirrorLineY1SpinBox);

    m_mirrorLineX2SpinBox = new QDoubleSpinBox();
    m_mirrorLineX2SpinBox->setRange(-10000, 10000);
    m_mirrorLineX2SpinBox->setSingleStep(0.1);
    m_mirrorLineX2SpinBox->setValue(0);
    customForm->addRow(trTransform("End X:"), m_mirrorLineX2SpinBox);

    m_mirrorLineY2SpinBox = new QDoubleSpinBox();
    m_mirrorLineY2SpinBox->setRange(-10000, 10000);
    m_mirrorLineY2SpinBox->setSingleStep(0.1);
    m_mirrorLineY2SpinBox->setValue(0);
    customForm->addRow(trTransform("End Y:"), m_mirrorLineY2SpinBox);

    customGroup->setVisible(false);
    groupLayout->addWidget(customGroup);

    // 连接信号：切换轴类型时显示/隐藏自定义参数
    QObject::connect(axisGroup, &QButtonGroup::buttonClicked, dialog, [customGroup](QAbstractButton* btn) {
        auto* group = btn->group();
        if (group)
        {
            customGroup->setVisible(group->checkedId() == 2);
        }
    });

    layout->addWidget(group);
}

void TransformDialogAdapter::addPreviewControls(QVBoxLayout* layout, QDialog* dialog)
{
    auto previewCheck = new QCheckBox(trTransform("Live Preview"));
    previewCheck->setChecked(m_showPreview);
    QObject::connect(previewCheck, &QCheckBox::toggled, dialog, [this](bool checked) {
        m_showPreview = checked;
    });
    layout->addWidget(previewCheck);
}

TransformParameters TransformDialogAdapter::collectParametersFromUI(QDialog* dialog)
{
    TransformParameters params;
    params.type = m_transformType;

    switch (m_transformType)
    {
    case TransformType::Move:
    {
        if (m_moveXSpinBox)
        {
            params.moveX = m_moveXSpinBox->value();
        }
        if (m_moveYSpinBox)
        {
            params.moveY = m_moveYSpinBox->value();
        }
        break;
    }
    case TransformType::Copy:
    {
        if (m_copyCountSpinBox)
        {
            params.copyCount = m_copyCountSpinBox->value();
        }
        if (m_copySpacingXSpinBox)
        {
            params.moveX = m_copySpacingXSpinBox->value();
        }
        if (m_copySpacingYSpinBox)
        {
            params.moveY = m_copySpacingYSpinBox->value();
        }
        break;
    }
    case TransformType::Rotate:
    {
        if (m_rotateAngleSpinBox)
        {
            params.rotateAngle = m_rotateAngleSpinBox->value();
        }
        if (m_rotateCenterXSpinBox)
        {
            params.anchorX = m_rotateCenterXSpinBox->value();
        }
        if (m_rotateCenterYSpinBox)
        {
            params.anchorY = m_rotateCenterYSpinBox->value();
        }
        break;
    }
    case TransformType::Mirror:
    {
        // 根据选择的轴设置参数
        auto axisGroup = dialog->findChild<QButtonGroup*>();
        if (axisGroup)
        {
            int axis = axisGroup->checkedId();
            if (axis == 0)
            {
                // X 轴
                params.mirrorAxis = 0;
                params.mirrorCenterX = 0;
                params.mirrorCenterY = 0;
            }
            else if (axis == 1)
            {
                // Y 轴
                params.mirrorAxis = 1;
                params.mirrorCenterX = 0;
                params.mirrorCenterY = 0;
            }
            else
            {
                // 自定义轴
                params.mirrorAxis = 2;
                if (m_mirrorLineX1SpinBox)
                {
                    params.mirrorLineX1 = m_mirrorLineX1SpinBox->value();
                }
                if (m_mirrorLineY1SpinBox)
                {
                    params.mirrorLineY1 = m_mirrorLineY1SpinBox->value();
                }
                if (m_mirrorLineX2SpinBox)
                {
                    params.mirrorLineX2 = m_mirrorLineX2SpinBox->value();
                }
                if (m_mirrorLineY2SpinBox)
                {
                    params.mirrorLineY2 = m_mirrorLineY2SpinBox->value();
                }
            }
        }
        break;
    }
    default:
        break;
    }

    params.isInteractive = false;
    params.needPreview = m_showPreview;

    return params;
}

bool TransformDialogAdapter::validateParameters(const TransformParameters& params) const
{
    switch (params.type)
    {
    case TransformType::Move:
    {
        return true;  // 移动操作总是有效的
    }
    case TransformType::Copy:
    {
        return params.copyCount > 0;
    }
    case TransformType::Rotate:
    {
        // 角度不能为 0
        return params.rotateAngle != 0.0;
    }
    case TransformType::Mirror:
    {
        return true;  // 镜像操作总是有效的
    }
    default:
        return false;
    }
}

void TransformDialogAdapter::updatePreview(const TransformParameters& params)
{
    if (!m_showPreview)
    {
        return;
    }

    // 更新预览
    if (m_parametersChangedCallback)
    {
        m_parametersChangedCallback(params);
    }
}