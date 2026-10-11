/**
 * @file TransformDialogAdapter.cpp
 * @brief TransformDialog 适配器实现
 */
#include "TransformDialogAdapter.h"
#include "SceneDocument2D.h"

#include "UI/Dlg/UiDialogLayoutHelper.h"
#include "UI/Dlg/UiDialogLayoutMetrics.h"
#include "UI/Dlg/LocalizableDialog.h"
#include "UI/Dlg/DialogTabOrder.h"

#include <QCoreApplication>
#include <QDialog>
#include <QObject>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QLabel>
#include <QCheckBox>
#include <QRadioButton>
#include <QButtonGroup>
#include <QGroupBox>
#include <QDialogButtonBox>
#include <QPushButton>

namespace
{
    QString trTransform(const char* text)
    {
        return QCoreApplication::translate("TransformDialog", text);
    }

    class TransformDialog : public LocalizableDialog
    {
    public:
        explicit TransformDialog(QWidget* parent = nullptr)
            : LocalizableDialog(parent)
        {
        }

    protected:
        void retranslateUi() override
        {
            // 子类在 TransformDialogAdapter 中实现具体重翻译逻辑
        }
    };
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
    auto dialog = new TransformDialog(m_parent);
    dialog->setWindowTitle(getDialogTitle());

    // 使用标准主布局
    QVBoxLayout* mainLayout = UiDialogLayoutHelper::createMainLayout(dialog);
    dialog->setLayout(mainLayout);

    // 根据变换类型添加输入控件
    switch (m_transformType)
    {
    case TransformType::Move:
        addMoveInputs(mainLayout);
        break;
    case TransformType::Copy:
        addCopyInputs(mainLayout);
        break;
    case TransformType::Rotate:
        addRotateInputs(mainLayout);
        break;
    case TransformType::Mirror:
        addMirrorInputs(mainLayout, dialog);
        break;
    default:
        break;
    }

    // 添加预览控件
    if (m_showPreview)
    {
        addPreviewControls(mainLayout, dialog);
    }

    // 添加状态标签
    m_statusLabel = new QLabel(trTransform("Enter parameters"));
    m_statusLabel->setObjectName("statusLabel");
    m_statusLabel->setWordWrap(true);
    mainLayout->addWidget(m_statusLabel);

    // 添加标准按钮
    m_okBtn = new QPushButton(trTransform("OK"), dialog);
    UiDialogLayoutHelper::setupStandardButton(m_okBtn, true);
    m_okBtn->setDefault(true);
    m_okBtn->setObjectName("okBtn");

    m_cancelBtn = new QPushButton(trTransform("Cancel"), dialog);
    UiDialogLayoutHelper::setupSecondaryButton(m_cancelBtn);
    m_cancelBtn->setObjectName("cancelBtn");

    m_applyBtn = new QPushButton(trTransform("Apply"), dialog);
    UiDialogLayoutHelper::setupSecondaryButton(m_applyBtn);
    m_applyBtn->setObjectName("applyBtn");

    QHBoxLayout* btnLayout = UiDialogLayoutHelper::createButtonLayout(
        dialog, m_okBtn, m_cancelBtn, QList<QPushButton*>{m_applyBtn});
    mainLayout->addLayout(btnLayout);

    // 连接信号
    QObject::connect(m_okBtn, &QPushButton::clicked, dialog, &QDialog::accept);
    QObject::connect(m_cancelBtn, &QPushButton::clicked, dialog, &QDialog::reject);
    QObject::connect(m_applyBtn, &QPushButton::clicked, dialog, [this, dialog]() {
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

    // 标准 Tab 顺序
    UiDialog::DialogTabOrder tabOrder(dialog);
    // 根据变换类型添加输入控件到 Tab 顺序
    switch (m_transformType)
    {
    case TransformType::Move:
        if (m_moveXSpinBox) tabOrder.addWidget(m_moveXSpinBox, 1, "MoveX");
        if (m_moveYSpinBox) tabOrder.addWidget(m_moveYSpinBox, 2, "MoveY");
        break;
    case TransformType::Copy:
        if (m_copyCountSpinBox) tabOrder.addWidget(m_copyCountSpinBox, 1, "CopyCount");
        if (m_copySpacingXSpinBox) tabOrder.addWidget(m_copySpacingXSpinBox, 2, "CopySpacingX");
        if (m_copySpacingYSpinBox) tabOrder.addWidget(m_copySpacingYSpinBox, 3, "CopySpacingY");
        break;
    case TransformType::Rotate:
        if (m_rotateAngleSpinBox) tabOrder.addWidget(m_rotateAngleSpinBox, 1, "RotateAngle");
        if (m_rotateCenterXSpinBox) tabOrder.addWidget(m_rotateCenterXSpinBox, 2, "RotateCenterX");
        if (m_rotateCenterYSpinBox) tabOrder.addWidget(m_rotateCenterYSpinBox, 3, "RotateCenterY");
        break;
    case TransformType::Mirror:
        // Radio buttons 和 spin boxes 会在 addMirrorInputs 中处理
        break;
    default:
        break;
    }
    tabOrder.addWidget(m_okBtn, 10, "OK")
            .addWidget(m_applyBtn, 11, "Apply")
            .addWidget(m_cancelBtn, 12, "Cancel");
    tabOrder.apply();

    // 自适应尺寸与居中
    dialog->setCenteredAdaptiveSize(UiDialogLayout::DialogDefaultWidthChars, UiDialogLayout::DialogDefaultHeightRows);

    return dialog;
}

void TransformDialogAdapter::addMoveInputs(QVBoxLayout* layout)
{
    auto group = new QGroupBox(trTransform("Move Parameters"));
    QVBoxLayout* groupLayout = UiDialogLayoutHelper::createGroupBoxLayout(group);

    m_moveXSpinBox = new QDoubleSpinBox();
    m_moveXSpinBox->setRange(-10000, 10000);
    m_moveXSpinBox->setSingleStep(0.1);
    m_moveXSpinBox->setValue(0);
    UiDialogLayoutHelper::setupStandardDoubleSpinBox(m_moveXSpinBox);
    m_moveXSpinBox->setObjectName("moveXSpinBox");

    m_moveYSpinBox = new QDoubleSpinBox();
    m_moveYSpinBox->setRange(-10000, 10000);
    m_moveYSpinBox->setSingleStep(0.1);
    m_moveYSpinBox->setValue(0);
    UiDialogLayoutHelper::setupStandardDoubleSpinBox(m_moveYSpinBox);
    m_moveYSpinBox->setObjectName("moveYSpinBox");

    QFormLayout* formLayout = UiDialogLayoutHelper::createFormLayout(group);
    formLayout->addRow(trTransform("X:"), m_moveXSpinBox);
    formLayout->addRow(trTransform("Y:"), m_moveYSpinBox);

    groupLayout->addLayout(formLayout);
    layout->addWidget(group);
}

void TransformDialogAdapter::addCopyInputs(QVBoxLayout* layout)
{
    auto group = new QGroupBox(trTransform("Copy Parameters"));
    QVBoxLayout* groupLayout = UiDialogLayoutHelper::createGroupBoxLayout(group);

    m_copyCountSpinBox = new QSpinBox();
    m_copyCountSpinBox->setRange(1, 100);
    m_copyCountSpinBox->setValue(1);
    UiDialogLayoutHelper::setupStandardSpinBox(m_copyCountSpinBox);
    m_copyCountSpinBox->setObjectName("copyCountSpinBox");

    m_copySpacingXSpinBox = new QDoubleSpinBox();
    m_copySpacingXSpinBox->setRange(-10000, 10000);
    m_copySpacingXSpinBox->setSingleStep(0.1);
    m_copySpacingXSpinBox->setValue(0);
    UiDialogLayoutHelper::setupStandardDoubleSpinBox(m_copySpacingXSpinBox);
    m_copySpacingXSpinBox->setObjectName("copySpacingXSpinBox");

    m_copySpacingYSpinBox = new QDoubleSpinBox();
    m_copySpacingYSpinBox->setRange(-10000, 10000);
    m_copySpacingYSpinBox->setSingleStep(0.1);
    m_copySpacingYSpinBox->setValue(0);
    UiDialogLayoutHelper::setupStandardDoubleSpinBox(m_copySpacingYSpinBox);
    m_copySpacingYSpinBox->setObjectName("copySpacingYSpinBox");

    QFormLayout* formLayout = UiDialogLayoutHelper::createFormLayout(group);
    formLayout->addRow(trTransform("Count:"), m_copyCountSpinBox);
    formLayout->addRow(trTransform("X Spacing:"), m_copySpacingXSpinBox);
    formLayout->addRow(trTransform("Y Spacing:"), m_copySpacingYSpinBox);

    groupLayout->addLayout(formLayout);
    layout->addWidget(group);
}

void TransformDialogAdapter::addRotateInputs(QVBoxLayout* layout)
{
    auto group = new QGroupBox(trTransform("Rotate Parameters"));
    QVBoxLayout* groupLayout = UiDialogLayoutHelper::createGroupBoxLayout(group);

    m_rotateAngleSpinBox = new QDoubleSpinBox();
    m_rotateAngleSpinBox->setRange(-360, 360);
    m_rotateAngleSpinBox->setSingleStep(0.1);
    m_rotateAngleSpinBox->setValue(0);
    m_rotateAngleSpinBox->setSuffix(trTransform("°"));
    UiDialogLayoutHelper::setupStandardDoubleSpinBox(m_rotateAngleSpinBox);
    m_rotateAngleSpinBox->setObjectName("rotateAngleSpinBox");

    m_rotateCenterXSpinBox = new QDoubleSpinBox();
    m_rotateCenterXSpinBox->setRange(-10000, 10000);
    m_rotateCenterXSpinBox->setSingleStep(0.1);
    m_rotateCenterXSpinBox->setValue(0);
    UiDialogLayoutHelper::setupStandardDoubleSpinBox(m_rotateCenterXSpinBox);
    m_rotateCenterXSpinBox->setObjectName("rotateCenterXSpinBox");

    m_rotateCenterYSpinBox = new QDoubleSpinBox();
    m_rotateCenterYSpinBox->setRange(-10000, 10000);
    m_rotateCenterYSpinBox->setSingleStep(0.1);
    m_rotateCenterYSpinBox->setValue(0);
    UiDialogLayoutHelper::setupStandardDoubleSpinBox(m_rotateCenterYSpinBox);
    m_rotateCenterYSpinBox->setObjectName("rotateCenterYSpinBox");

    QFormLayout* formLayout = UiDialogLayoutHelper::createFormLayout(group);
    formLayout->addRow(trTransform("Angle:"), m_rotateAngleSpinBox);
    formLayout->addRow(trTransform("Center X:"), m_rotateCenterXSpinBox);
    formLayout->addRow(trTransform("Center Y:"), m_rotateCenterYSpinBox);

    groupLayout->addLayout(formLayout);
    layout->addWidget(group);
}

void TransformDialogAdapter::addMirrorInputs(QVBoxLayout* layout, QDialog* dialog)
{
    auto group = new QGroupBox(trTransform("Mirror Parameters"));
    QVBoxLayout* groupLayout = UiDialogLayoutHelper::createGroupBoxLayout(group);

    // 镜像轴选择
    auto axisGroup = new QButtonGroup(group);

    auto xRadio = new QRadioButton(trTransform("X Axis"));
    xRadio->setObjectName("mirrorXRadio");
    axisGroup->addButton(xRadio, 0);
    groupLayout->addWidget(xRadio);

    auto yRadio = new QRadioButton(trTransform("Y Axis"));
    yRadio->setObjectName("mirrorYRadio");
    axisGroup->addButton(yRadio, 1);
    yRadio->setChecked(true);
    groupLayout->addWidget(yRadio);

    auto customRadio = new QRadioButton(trTransform("Custom Axis"));
    customRadio->setObjectName("mirrorCustomRadio");
    axisGroup->addButton(customRadio, 2);
    groupLayout->addWidget(customRadio);

    // 自定义轴输入（默认隐藏）
    auto customGroup = new QGroupBox(trTransform("Custom Axis Parameters"));
    QVBoxLayout* customGroupLayout = UiDialogLayoutHelper::createGroupBoxLayout(customGroup);

    m_mirrorLineX1SpinBox = new QDoubleSpinBox();
    m_mirrorLineX1SpinBox->setRange(-10000, 10000);
    m_mirrorLineX1SpinBox->setSingleStep(0.1);
    m_mirrorLineX1SpinBox->setValue(0);
    UiDialogLayoutHelper::setupStandardDoubleSpinBox(m_mirrorLineX1SpinBox);
    m_mirrorLineX1SpinBox->setObjectName("mirrorLineX1SpinBox");

    m_mirrorLineY1SpinBox = new QDoubleSpinBox();
    m_mirrorLineY1SpinBox->setRange(-10000, 10000);
    m_mirrorLineY1SpinBox->setSingleStep(0.1);
    m_mirrorLineY1SpinBox->setValue(0);
    UiDialogLayoutHelper::setupStandardDoubleSpinBox(m_mirrorLineY1SpinBox);
    m_mirrorLineY1SpinBox->setObjectName("mirrorLineY1SpinBox");

    m_mirrorLineX2SpinBox = new QDoubleSpinBox();
    m_mirrorLineX2SpinBox->setRange(-10000, 10000);
    m_mirrorLineX2SpinBox->setSingleStep(0.1);
    m_mirrorLineX2SpinBox->setValue(0);
    UiDialogLayoutHelper::setupStandardDoubleSpinBox(m_mirrorLineX2SpinBox);
    m_mirrorLineX2SpinBox->setObjectName("mirrorLineX2SpinBox");

    m_mirrorLineY2SpinBox = new QDoubleSpinBox();
    m_mirrorLineY2SpinBox->setRange(-10000, 10000);
    m_mirrorLineY2SpinBox->setSingleStep(0.1);
    m_mirrorLineY2SpinBox->setValue(0);
    UiDialogLayoutHelper::setupStandardDoubleSpinBox(m_mirrorLineY2SpinBox);
    m_mirrorLineY2SpinBox->setObjectName("mirrorLineY2SpinBox");

    QFormLayout* customForm = UiDialogLayoutHelper::createFormLayout(customGroup);
    customForm->addRow(trTransform("Start X:"), m_mirrorLineX1SpinBox);
    customForm->addRow(trTransform("Start Y:"), m_mirrorLineY1SpinBox);
    customForm->addRow(trTransform("End X:"), m_mirrorLineX2SpinBox);
    customForm->addRow(trTransform("End Y:"), m_mirrorLineY2SpinBox);

    customGroupLayout->addLayout(customForm);
    customGroup->setVisible(false);
    groupLayout->addWidget(customGroup);

    // 连接信号：切换轴类型时显示/隐藏自定义参数
    QObject::connect(axisGroup, &QButtonGroup::idClicked, dialog, [customGroup](int id) {
        customGroup->setVisible(id == 2);
    });

    layout->addWidget(group);
}

void TransformDialogAdapter::addPreviewControls(QVBoxLayout* layout, QDialog* dialog)
{
    auto previewCheck = new QCheckBox(trTransform("Live Preview"));
    previewCheck->setChecked(m_showPreview);
    previewCheck->setObjectName("previewCheckBox");
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