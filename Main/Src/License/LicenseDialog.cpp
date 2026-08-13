#include "LicenseDialog.h"

#include "License/LicenseDLL.h"

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
    setFixedSize(520, 300);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);
    mainLayout->setContentsMargins(24, 24, 24, 24);

    auto* titleLabel = new QLabel(tr("<h2>Activate License</h2>"));
    titleLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(titleLabel);

    auto* machineCodeLayout = new QHBoxLayout();
    auto* mcLabel = new QLabel(tr("Machine Code:"));
    m_machineCodeLabel = new QLabel(m_machineCode);
    m_machineCodeLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_machineCodeLabel->setStyleSheet(
        QStringLiteral("font-family: monospace; padding: 4px; background: #f0f0f0; border: 1px solid #ccc;"));
    machineCodeLayout->addWidget(mcLabel);
    machineCodeLayout->addWidget(m_machineCodeLabel, 1);
    mainLayout->addLayout(machineCodeLayout);

    auto* regLayout = new QHBoxLayout();
    auto* regLabel = new QLabel(tr("Reg Code:"));
    m_regCodeEdit = new QLineEdit();
    m_regCodeEdit->setPlaceholderText(tr("Paste your registration code here"));
    regLayout->addWidget(regLabel);
    regLayout->addWidget(m_regCodeEdit, 1);
    mainLayout->addLayout(regLayout);

    m_statusLabel = new QLabel();
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setStyleSheet(QStringLiteral("color: red;"));
    mainLayout->addWidget(m_statusLabel);

    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();

    m_activateBtn = new QPushButton(tr("Activate"));
    m_activateBtn->setDefault(true);
    connect(m_activateBtn, &QPushButton::clicked, this, &LicenseDialog::OnActivateClicked);

    m_exitBtn = new QPushButton(tr("Exit"));
    connect(m_exitBtn, &QPushButton::clicked, this, &QDialog::reject);

    btnLayout->addWidget(m_activateBtn);
    btnLayout->addWidget(m_exitBtn);
    mainLayout->addLayout(btnLayout);

    if (!m_machineCode.isEmpty())
    {
        auto* copyBtn = new QPushButton(tr("Copy Machine Code"));
        connect(copyBtn, &QPushButton::clicked, this, [this]() {
            QApplication::clipboard()->setText(m_machineCode);
        });
        btnLayout->insertWidget(1, copyBtn);
    }
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
    m_statusLabel->setStyleSheet(QStringLiteral("color: gray;"));
    m_statusLabel->setText(tr("Verifying..."));

    QApplication::processEvents();

    LicenseContextHolder holder(m_configDir);
    if (!holder.get())
    {
        m_statusLabel->setStyleSheet(QStringLiteral("color: red;"));
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

    m_statusLabel->setStyleSheet(QStringLiteral("color: red;"));
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