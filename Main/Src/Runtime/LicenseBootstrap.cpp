#include "LicenseBootstrap.h"

#include "License/LicenseDLL.h"
#include "License/TrialManager.h"
#include "License/LicenseDialog.h"
#include "UI/ClientConfig/UiFeatureGate.h"

#include <QMessageBox>
#include <QPushButton>
#include <QDialog>
#include <QWidget>

int LicenseBootstrap::run(const QString& configDir, QWidget* parentWidget)
{
    LicenseConfig config{};
    License_ConfigInit(&config);
    const QByteArray configDirUtf8 = configDir.toUtf8();
    config.configDir = configDirUtf8.constData();

    Trial_SetConfigDir(configDirUtf8.constData());

    int remainingDays = 0;
    const int trialResult = Trial_Check(&remainingDays);
    bool inTrialMode = false;

    SY_DEBUGF("[LicenseBootstrap] Trial check: result=%d, remainingDays=%d", trialResult, remainingDays);

    if (trialResult == -1)
    {
        int expiredChoice = -1;
        QMessageBox expiredBox(QMessageBox::Warning,
            QString::fromUtf8("Trial Expired"),
            QString::fromUtf8("Your trial period has ended.\n\nPlease register to continue using the software."),
            QMessageBox::NoButton,
            parentWidget);
        QPushButton* regBtn = expiredBox.addButton(QString::fromUtf8("Register Now"), QMessageBox::AcceptRole);
        QPushButton* exitBtn = expiredBox.addButton(QString::fromUtf8("Exit"), QMessageBox::RejectRole);
        QObject::connect(regBtn, &QPushButton::clicked, [&expiredChoice]() { expiredChoice = 0; });
        QObject::connect(exitBtn, &QPushButton::clicked, [&expiredChoice]() { expiredChoice = 1; });
        expiredBox.exec();

        if (expiredChoice != 0)
        {
            SY_INFO("[LicenseBootstrap] Trial expired: user chose to exit");
            return -1;
        }

        LicenseDialog dlg(configDir);
        if (dlg.exec() != QDialog::Accepted)
        {
            SY_WARN("[LicenseBootstrap] Trial expired: user cancelled activation, exiting");
            return -1;
        }
        SY_INFO("[LicenseBootstrap] Trial expired: user activated license successfully");
    }
    else if (trialResult == 0 && remainingDays > 0)
    {
        UiFeatureGate::instance().setUnrestricted(true);
        SY_INFOF("[LicenseBootstrap] Trial active: %d days remaining", remainingDays);

        auto tr = [](const char* text) {
            return QCoreApplication::translate("LicenseBootstrap", text);
        };

        QString trialMsg = tr("Trial Version\n\n"
            "You have %1 day(s) remaining in your trial period.\n\n"
            "You can continue using the trial version or activate a license now.")
            .arg(remainingDays);

        int trialChoice = -1;
        QMessageBox trialBox(QMessageBox::Information,
            tr("Trial Period"),
            trialMsg,
            QMessageBox::NoButton,
            parentWidget);
        QPushButton* regBtn = trialBox.addButton(tr("Register Now"), QMessageBox::AcceptRole);
        QPushButton* contBtn = trialBox.addButton(tr("Continue Trial"), QMessageBox::RejectRole);
        QObject::connect(regBtn, &QPushButton::clicked, [&trialChoice]() { trialChoice = 0; });
        QObject::connect(contBtn, &QPushButton::clicked, [&trialChoice]() { trialChoice = 1; });
        trialBox.exec();

        SY_DEBUGF("[LicenseBootstrap] Trial dialog choice=%d", trialChoice);

        if (trialChoice == 0)
        {
            LicenseDialog dlg(configDir);
            if (dlg.exec() == QDialog::Accepted)
            {
                SY_INFO("[LicenseBootstrap] Trial active: user activated license, upgrading to licensed mode");
            }
            else
            {
                SY_INFO("[LicenseBootstrap] Trial active: user cancelled activation, continuing in trial mode");
                inTrialMode = true;
            }
        }
        else
        {
            SY_INFO("[LicenseBootstrap] Trial active: user chose to continue trial");
            inTrialMode = true;
        }
    }
    else
    {
        SY_WARN("[LicenseBootstrap] Trial check unexpected state, falling back to trial mode");
        inTrialMode = true;
        UiFeatureGate::instance().setUnrestricted(true);
    }

    if (!inTrialMode && License_IsCheckEnabled())
    {
        LicenseContext* licenseCtx = License_Create(&config);
        const bool licenseOk = licenseCtx && License_Check(licenseCtx) == LICENSE_OK;

        if (licenseOk && licenseCtx)
        {
            LicenseInfo info{};
            info.structSize = sizeof(LicenseInfo);
            if (License_GetInfo(licenseCtx, &info) == LICENSE_OK)
            {
                UiFeatureGate::instance().loadFromLicenseString(QString::fromUtf8(info.features));
                SY_INFOF("[LicenseBootstrap] License features applied: customer='%s' expiry='%s'",
                    info.customerName,
                    info.expiryDate);
            }
            else
            {
                SY_WARN("[LicenseBootstrap] License_GetInfo failed, feature gate stays unrestricted");
                UiFeatureGate::instance().setUnrestricted(true);
            }
        }

        if (licenseCtx)
        {
            License_Destroy(licenseCtx);
        }

        if (!licenseOk)
        {
            SY_DEBUG("[LicenseBootstrap] License check failed, showing license dialog");
            LicenseDialog dlg(configDir);
            if (dlg.exec() != QDialog::Accepted)
            {
                SY_WARN("[LicenseBootstrap] License check rejected by user");
                return -3;
            }
            SY_DEBUG("[LicenseBootstrap] License accepted by user");

            LicenseContext* activatedCtx = License_Create(&config);
            if (activatedCtx)
            {
                LicenseInfo info{};
                info.structSize = sizeof(LicenseInfo);
                if (License_Check(activatedCtx) == LICENSE_OK && License_GetInfo(activatedCtx, &info) == LICENSE_OK)
                {
                    UiFeatureGate::instance().loadFromLicenseString(QString::fromUtf8(info.features));
                    SY_INFOF("[LicenseBootstrap] License features applied after activation: customer='%s'",
                        info.customerName);
                }
                License_Destroy(activatedCtx);
            }
        }
    }
    else if (!inTrialMode)
    {
        UiFeatureGate::instance().setUnrestricted(true);
    }

    return 0;
}
