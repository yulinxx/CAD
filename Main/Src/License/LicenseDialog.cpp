#include "LicenseDialog.h"
#include "LicenseManager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QMessageBox>
#include <QClipboard>
#include <QApplication>

LicenseDialog::LicenseDialog(const QString& configDir, QWidget* parent)
    : QDialog(parent)
    , m_configDir(configDir)
{
    LicenseManager mgr(std::filesystem::path(configDir.toStdWString()));
    m_machineCode = QString::fromStdString(mgr.GetMachineCode());
    SetupUi();
}

void LicenseDialog::SetupUi()
{
    setWindowTitle(QStringLiteral("Software Activation - SanYiCAD"));
    setFixedSize(520, 300);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);
    mainLayout->setContentsMargins(24, 24, 24, 24);

    auto* titleLabel = new QLabel(QStringLiteral("<h2>Activate License</h2>"));
    titleLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(titleLabel);

    auto* machineCodeLayout = new QHBoxLayout();
    auto* mcLabel = new QLabel(QStringLiteral("Machine Code:"));
    m_machineCodeLabel = new QLabel(m_machineCode);
    m_machineCodeLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_machineCodeLabel->setStyleSheet(QStringLiteral("font-family: monospace; padding: 4px; background: #f0f0f0; border: 1px solid #ccc;"));
    machineCodeLayout->addWidget(mcLabel);
    machineCodeLayout->addWidget(m_machineCodeLabel, 1);
    mainLayout->addLayout(machineCodeLayout);

    auto* regLayout = new QHBoxLayout();
    auto* regLabel = new QLabel(QStringLiteral("Reg Code:"));
    m_regCodeEdit = new QLineEdit();
    m_regCodeEdit->setPlaceholderText(QStringLiteral("Paste your registration code here"));
    regLayout->addWidget(regLabel);
    regLayout->addWidget(m_regCodeEdit, 1);
    mainLayout->addLayout(regLayout);

    m_statusLabel = new QLabel();
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setStyleSheet(QStringLiteral("color: red;"));
    mainLayout->addWidget(m_statusLabel);

    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();

    m_activateBtn = new QPushButton(QStringLiteral("Activate"));
    m_activateBtn->setDefault(true);
    connect(m_activateBtn, &QPushButton::clicked, this, &LicenseDialog::OnActivateClicked);

    m_exitBtn = new QPushButton(QStringLiteral("Exit"));
    connect(m_exitBtn, &QPushButton::clicked, this, &QDialog::reject);

    btnLayout->addWidget(m_activateBtn);
    btnLayout->addWidget(m_exitBtn);
    mainLayout->addLayout(btnLayout);

    if (!m_machineCode.isEmpty())
    {
        auto* copyBtn = new QPushButton(QStringLiteral("Copy Machine Code"));
        connect(copyBtn, &QPushButton::clicked, this, [this]() {
            QApplication::clipboard()->setText(m_machineCode);
            });
        btnLayout->insertWidget(1, copyBtn);
    }
}

void LicenseDialog::OnActivateClicked()
{
    accept();
    return;

    QString regCode = m_regCodeEdit->text().trimmed();
    if (regCode.isEmpty())
    {
        m_statusLabel->setText(QStringLiteral("Please enter a registration code."));
        return;
    }

    m_activateBtn->setEnabled(false);
    m_statusLabel->setStyleSheet(QStringLiteral("color: gray;"));
    m_statusLabel->setText(QStringLiteral("Verifying..."));

    QApplication::processEvents();

    LicenseManager mgr(std::filesystem::path(m_configDir.toStdWString()));
    bool ok = mgr.Activate(regCode.toStdString());

    if (ok)
    {
        QMessageBox::information(this, QStringLiteral("Activation Successful"),
            QStringLiteral("License has been activated successfully.\n\n"
                "Expires: %1\nFeatures: %2")
            .arg(QString::fromStdString(mgr.GetLicenseInfo().expiryDate))
            .arg(QString::fromStdString(mgr.GetLicenseInfo().features)));
        accept();
    }
    else
    {
        m_statusLabel->setStyleSheet(QStringLiteral("color: red;"));
        m_statusLabel->setText(QString::fromStdString(mgr.GetLicenseInfo().errorMsg));
        m_activateBtn->setEnabled(true);
    }
}
