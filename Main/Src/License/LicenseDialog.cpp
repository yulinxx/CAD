#include "LicenseDialog.h"

#include "License/LicenseDLL.h"
#include "UI/ThemeManager.h"
#include "UI/Dlg/UiDialogLayoutHelper.h"
#include "UI/Dlg/UiDialogLayoutMetrics.h"
#include "UI/Dlg/DialogTabOrder.h"

#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QFormLayout>

namespace
{
    class LicenseContextHolder
    {
    public:
        explicit LicenseContextHolder(const QString& configDir)
        {
            License_ConfigInit(&m_config);
            m_configDirUtf8 = configDir.toUtf8();
            m_config.configDir = m_configDirUtf8.constData();
            m_context = License_Create(&m_config);
        }

        ~LicenseContextHolder()
        {
            License_Destroy(m_context);
        }

        LicenseContext* get() const
        {
            return m_context;
        }

    private:
        LicenseConfig m_config{};
        QByteArray m_configDirUtf8;
        LicenseContext* m_context = nullptr;
    };
}  // namespace

LicenseDialog::LicenseDialog(const QString& configDir, QWidget* parent)
    : LocalizableDialog(parent)
    , m_configDir(configDir)
{
    // 设置焦点策略，确保 Tab 键可以切换焦点（macOS 默认行为不同）
    setFocusPolicy(Qt::StrongFocus);
    LicenseContextHolder holder(m_configDir);
    if (holder.get())
    {
        char machineCode[128] = {};
        if (License_GetMachineCode(holder.get(), machineCode, sizeof(machineCode)) == LICENSE_OK)
        {
            m_machineCode = QString::fromUtf8(machineCode);
        }
    }

    SetupUi();

    // 自适应尺寸与居中
    setCenteredAdaptiveSize(UiDialogLayout::DialogDefaultWidthChars, UiDialogLayout::DialogDefaultHeightRows);

    // 标准 Tab 顺序
    UiDialog::DialogTabOrder tabOrder(this);
    tabOrder.addWidget(m_regCodeEdit, 1, "RegCodeEdit")
        .addWidget(m_activateBtn, 10, "ActivateBtn")
        .addWidget(m_exitBtn, 11, "ExitBtn");
    if (m_machineCodeLabel)
    {
        tabOrder.addWidget(m_machineCodeLabel, 0, "MachineCodeLabel");
    }
    tabOrder.apply();
}

void LicenseDialog::SetupUi()
{
    setWindowTitle(tr("Software Activation - SanYiCAD"));
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    // 使用辅助工具创建标准主布局
    QVBoxLayout* mainLayout = UiDialogLayoutHelper::createMainLayout(this);
    setLayout(mainLayout);

    auto* titleLabel = new QLabel(tr("<h2>Activate License</h2>"), this);
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setObjectName("titleLabel");
    mainLayout->addWidget(titleLabel);

    // 使用标准表单布局
    QFormLayout* formLayout = UiDialogLayoutHelper::createFormLayout(this);

    // 机器码行
    auto* mcLabel = new QLabel(tr("Machine Code:"), this);
    m_machineCodeLabel = new QLabel(m_machineCode, this);
    m_machineCodeLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_machineCodeLabel->setObjectName("machineCodeLabel");
    formLayout->addRow(mcLabel, m_machineCodeLabel);

    // 注册码行
    auto* regLabel = new QLabel(tr("Reg Code:"), this);
    m_regCodeEdit = new QLineEdit(this);
    UiDialogLayoutHelper::setControlHeight(m_regCodeEdit);
    m_regCodeEdit->setPlaceholderText(tr("Paste your registration code here"));
    m_regCodeEdit->setObjectName("regCodeEdit");
    formLayout->addRow(regLabel, m_regCodeEdit);

    mainLayout->addLayout(formLayout);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setObjectName("statusLabel");
    mainLayout->addWidget(m_statusLabel);

    // 按钮行 - 使用标准按钮布局
    m_activateBtn = new QPushButton(tr("Activate"), this);
    UiDialogLayoutHelper::setupStandardButton(m_activateBtn, true);
    m_activateBtn->setDefault(true);
    m_activateBtn->setObjectName("activateBtn");
    connect(m_activateBtn, &QPushButton::clicked, this, &LicenseDialog::OnActivateClicked);

    m_exitBtn = new QPushButton(tr("Exit"), this);
    UiDialogLayoutHelper::setupSecondaryButton(m_exitBtn);
    m_exitBtn->setObjectName("exitBtn");
    connect(m_exitBtn, &QPushButton::clicked, this, &QDialog::reject);

    QList<QPushButton*> extraButtons;
    if (!m_machineCode.isEmpty())
    {
        auto* copyBtn = new QPushButton(tr("Copy Machine Code"), this);
        UiDialogLayoutHelper::setupSecondaryButton(copyBtn);
        copyBtn->setObjectName("copyMachineCodeBtn");
        connect(copyBtn, &QPushButton::clicked, this, [this]() {
            QApplication::clipboard()->setText(m_machineCode);
        });
        extraButtons.append(copyBtn);
    }

    QHBoxLayout* btnLayout = UiDialogLayoutHelper::createButtonLayout(this, m_activateBtn, m_exitBtn, extraButtons);
    mainLayout->addLayout(btnLayout);
}

void LicenseDialog::retranslateUi()
{
    setWindowTitle(tr("Software Activation - SanYiCAD"));

    if (auto* titleLabel = findChild<QLabel*>("titleLabel"))
    {
        titleLabel->setText(tr("<h2>Activate License</h2>"));
    }

    if (findChild<QLabel*>("machineCodeLabel"))
    {
        // 标签文本在 formLayout 中，需要找到对应的 label
        // 这里通过 objectName 找不到 QFormLayout 的 label，直接重新设置
    }

    // 重新翻译表单标签
    if (auto* formLayout = qobject_cast<QFormLayout*>(layout()->itemAt(1)->layout()))
    {
        if (QWidget* labelWidget = formLayout->labelForField(m_machineCodeLabel))
        {
            if (auto* label = qobject_cast<QLabel*>(labelWidget))
            {
                label->setText(tr("Machine Code:"));
            }
        }
        if (QWidget* labelWidget = formLayout->labelForField(m_regCodeEdit))
        {
            if (auto* label = qobject_cast<QLabel*>(labelWidget))
            {
                label->setText(tr("Reg Code:"));
            }
        }
    }

    m_regCodeEdit->setPlaceholderText(tr("Paste your registration code here"));
    m_activateBtn->setText(tr("Activate"));
    m_exitBtn->setText(tr("Exit"));

    if (auto* copyBtn = findChild<QPushButton*>("copyMachineCodeBtn"))
    {
        copyBtn->setText(tr("Copy Machine Code"));
    }

    // 状态标签的错误文本会在 OnActivateClicked 中重新设置
    // 这里只保持当前文本（如果是错误状态会在下次操作时更新）
}

void LicenseDialog::OnActivateClicked()
{
    const QString regCode = m_regCodeEdit->text().trimmed();
    if (regCode.isEmpty())
    {
        m_statusLabel->setText(tr("Please enter a registration code."));
        return;
    }

    m_activateBtn->setEnabled(false);
    m_statusLabel->setStyleSheet(QStringLiteral("color: %1;").arg(TM->colors().textMuted));
    m_statusLabel->setText(tr("Verifying..."));

    QApplication::processEvents();

    LicenseContextHolder holder(m_configDir);
    if (!holder.get())
    {
        m_statusLabel->setStyleSheet(QStringLiteral("color: %1;").arg(TM->colors().error));
        m_statusLabel->setText(tr("Failed to initialize license module."));
        m_activateBtn->setEnabled(true);
        return;
    }

    const QByteArray regCodeUtf8 = regCode.toUtf8();
    const int activateResult = License_Activate(holder.get(), regCodeUtf8.constData());
    if (activateResult == LICENSE_OK)
    {
        LicenseInfo info{};
        info.structSize = sizeof(LicenseInfo);
        License_GetInfo(holder.get(), &info);

        QMessageBox::information(this,
            tr("Activation Successful"),
            tr("License has been activated successfully.\n\n"
               "Expires: %1\nFeatures: %2")
                .arg(QString::fromUtf8(info.expiryDate))
                .arg(QString::fromUtf8(info.features)));
        accept();
        return;
    }

    char errMsg[512] = {};
    License_GetLastErrorMessage(errMsg, sizeof(errMsg));

    LicenseInfo info{};
    info.structSize = sizeof(LicenseInfo);
    License_GetInfo(holder.get(), &info);

    m_statusLabel->setStyleSheet(QStringLiteral("color: %1;").arg(TM->colors().error));
    if (info.errorMessage[0] != '\0')
    {
        m_statusLabel->setText(QString::fromUtf8(info.errorMessage));
    }
    else if (errMsg[0] != '\0')
    {
        m_statusLabel->setText(QString::fromUtf8(errMsg));
    }
    else
    {
        m_statusLabel->setText(tr("Activation failed."));
    }
    m_activateBtn->setEnabled(true);
}