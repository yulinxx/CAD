#include "LicenseDialog.h"

#include "License/LicenseDLL.h"
#include "UI/ThemeManager.h"
#include "UI/Dlg/UiDialogLayoutHelper.h"
#include "UI/Dlg/UiDialogLayoutMetrics.h"

#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

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
    : QDialog(parent)
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
    mainLayout->addWidget(titleLabel);

    // 机器码行
    auto* machineCodeLayout = new QHBoxLayout();
    machineCodeLayout->setSpacing(UiDialogLayout::FormSpacing);
    auto* mcLabel = new QLabel(tr("Machine Code:"), this);
    m_machineCodeLabel = new QLabel(m_machineCode, this);
    m_machineCodeLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_machineCodeLabel->setStyleSheet(
        QStringLiteral("font-family: monospace; padding: 4px; background: %1; border: 1px solid %2;")
            .arg(TM->colors().iconBg, TM->colors().borderNormal));
    machineCodeLayout->addWidget(mcLabel);
    machineCodeLayout->addWidget(m_machineCodeLabel, 1);
    mainLayout->addLayout(machineCodeLayout);

    // 注册码行
    auto* regLayout = new QHBoxLayout();
    regLayout->setSpacing(UiDialogLayout::FormSpacing);
    auto* regLabel = new QLabel(tr("Reg Code:"), this);
    m_regCodeEdit = new QLineEdit(this);
    UiDialogLayoutHelper::setControlHeight(m_regCodeEdit);
    m_regCodeEdit->setPlaceholderText(tr("Paste your registration code here"));
    regLayout->addWidget(regLabel);
    regLayout->addWidget(m_regCodeEdit, 1);
    mainLayout->addLayout(regLayout);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setStyleSheet(QStringLiteral("color: %1;").arg(TM->colors().error));
    mainLayout->addWidget(m_statusLabel);

    // 按钮行
    auto* btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(UiDialogLayout::ButtonSpacing);

    m_activateBtn = new QPushButton(tr("Activate"), this);
    UiDialogLayoutHelper::setupStandardButton(m_activateBtn, true);
    m_activateBtn->setDefault(true);
    connect(m_activateBtn, &QPushButton::clicked, this, &LicenseDialog::OnActivateClicked);

    m_exitBtn = new QPushButton(tr("Exit"), this);
    UiDialogLayoutHelper::setupSecondaryButton(m_exitBtn);
    connect(m_exitBtn, &QPushButton::clicked, this, &QDialog::reject);

    btnLayout->addStretch();
    btnLayout->addWidget(m_activateBtn);
    btnLayout->addWidget(m_exitBtn);

    if (!m_machineCode.isEmpty())
    {
        auto* copyBtn = new QPushButton(tr("Copy Machine Code"), this);
        UiDialogLayoutHelper::setupSecondaryButton(copyBtn);
        connect(copyBtn, &QPushButton::clicked, this, [this]() {
            QApplication::clipboard()->setText(m_machineCode);
        });
        // 插入到激活按钮之前
        btnLayout->insertWidget(1, copyBtn);
    }

    mainLayout->addLayout(btnLayout);
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