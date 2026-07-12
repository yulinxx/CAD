#pragma once

#include <QDialog>
#include <QString>

class QLineEdit;
class QLabel;
class QPushButton;

class LicenseDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LicenseDialog(const QString& configDir, QWidget* parent = nullptr);
    ~LicenseDialog() override = default;

private slots:
    void OnActivateClicked();

private:
    void SetupUi();

    QLineEdit* m_regCodeEdit = nullptr;
    QLabel* m_machineCodeLabel = nullptr;
    QLabel* m_statusLabel = nullptr;
    QPushButton* m_activateBtn = nullptr;
    QPushButton* m_exitBtn = nullptr;

    QString m_configDir;
    QString m_machineCode;
};
