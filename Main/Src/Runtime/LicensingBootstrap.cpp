/**
 * @file LicensingBootstrap.cpp
 * @brief 启动期授权闸门实现 — 逻辑自 CADApplicationRuntime::run 原样迁移（P2-2.7）
 */
#include "LicensingBootstrap.h"

#include <QCoreApplication>
#include <QMessageBox>
#include <QObject>
#include <QPushButton>

#include "License/LicenseDialog.h"
#include "License/LicenseDLL.h"
#include "License/TrialManager.h"
#include "Log/SyLogger.h"
#include "UI/ClientConfig/UiFeatureGate.h"

namespace
{
    struct LicenseProbe
    {
        bool checkEnabled = false;
        bool valid = false;
        bool hasInfo = false;
        LicenseInfo info{};
    };

    // 只读探测：不写任何持久化状态，上下文在本函数内创建并销毁
    LicenseProbe probeLicense(const LicenseConfig& config)
    {
        LicenseProbe probe;
        probe.info.structSize = sizeof(LicenseInfo);
        probe.checkEnabled = License_IsCheckEnabled() != 0;
        if (!probe.checkEnabled)
        {
            return probe;
        }

        LicenseContext* ctx = License_Create(&config);
        if (!ctx)
        {
            SY_WARN("[LicensingBootstrap] License_Create failed, treating as unlicensed");
            return probe;
        }

        probe.valid = (License_Check(ctx) == LICENSE_OK);
        if (probe.valid)
        {
            probe.hasInfo = (License_GetInfo(ctx, &probe.info) == LICENSE_OK);
        }
        License_Destroy(ctx);
        return probe;
    }

    void applyLicensedFeatures(const LicenseProbe& probe)
    {
        if (!probe.hasInfo)
        {
            SY_WARN("[LicensingBootstrap] License_GetInfo failed, feature gate stays unrestricted");
            UiFeatureGate::instance().setUnrestricted(true);
            return;
        }

        UiFeatureGate::instance().loadFromLicenseString(QString::fromUtf8(probe.info.features));
        SY_INFOF("[LicensingBootstrap] License features applied: customer='%s' expiry='%s'",
            probe.info.customerName,
            probe.info.expiryDate);
    }
}  // namespace

int LicensingBootstrap::runGate(const QString& configDir)
{
    LicenseConfig config{};
    License_ConfigInit(&config);
    const QByteArray configDirUtf8 = configDir.toUtf8();
    config.configDir = configDirUtf8.constData();

    // === Phase 0: 许可证预检 ===
    // 有效许可证或未启用校验的构建直接返回：Trial_Check() 首次调用在无记录时
    // 会写入 trial.info（Windows 还写注册表），有授权时不应产生这份状态。
    const LicenseProbe license = probeLicense(config);

    if (!license.checkEnabled)
    {
        SY_INFO("[LicensingBootstrap] License check disabled: unrestricted");
        UiFeatureGate::instance().setUnrestricted(true);
        return 0;
    }

    if (license.valid)
    {
        SY_INFO("[LicensingBootstrap] Valid license found, skipping trial checks");
        applyLicensedFeatures(license);
        return 0;
    }

    // === Phase 1: 试用期检查（仅无有效许可证时执行） ===
    Trial_SetConfigDir(configDirUtf8.constData());

    int remainingDays = 0;
    const int trialResult = Trial_Check(&remainingDays);
    bool inTrialMode = false;  // 标记用户是否处于试用模式（未持有有效许可证）

    SY_DEBUGF("[LicensingBootstrap] Trial check: result=%d, remainingDays=%d", trialResult, remainingDays);

    if (trialResult == -1)
    {
        // 试用期已结束：必须注册或退出
        int expiredChoice = -1;  // 0=Register, 1=Exit
        QMessageBox expiredBox(QMessageBox::Warning,
            QString::fromUtf8("Trial Expired"),
            QString::fromUtf8("Your trial period has ended.\n\nPlease register to continue using the software."),
            QMessageBox::NoButton,
            nullptr);
        QPushButton* regBtn = expiredBox.addButton(QString::fromUtf8("Register Now"), QMessageBox::AcceptRole);
        QPushButton* exitBtn = expiredBox.addButton(QString::fromUtf8("Exit"), QMessageBox::RejectRole);
        QObject::connect(regBtn, &QPushButton::clicked, [&expiredChoice]() {
            expiredChoice = 0;
        });
        QObject::connect(exitBtn, &QPushButton::clicked, [&expiredChoice]() {
            expiredChoice = 1;
        });
        expiredBox.exec();

        if (expiredChoice != 0)
        {
            SY_INFO("[LicensingBootstrap] Trial expired: user chose to exit");
            return -1;
        }

        // 用户选择注册 → 激活成功才继续，否则退出
        LicenseDialog dlg(configDir);
        if (dlg.exec() != QDialog::Accepted)
        {
            SY_WARN("[LicensingBootstrap] Trial expired: user cancelled activation, exiting");
            return -1;
        }
        SY_INFO("[LicensingBootstrap] Trial expired: user activated license successfully");
        // 激活成功，继续执行下面的 License 检查来加载 features
    }
    else if (trialResult == 0 && remainingDays > 0)
    {
        // 试用期活跃：提示剩余天数，可选择注册或继续试用
        UiFeatureGate::instance().setUnrestricted(true);
        SY_INFOF("[LicensingBootstrap] Trial active: %d days remaining", remainingDays);

        // 翻译上下文保持 "CADApplicationRuntime"：既有 .ts 翻译按该 context 键控，
        // 类迁移不改变用户可见文案的翻译归属。
        auto tr = [](const char* text) {
            return QCoreApplication::translate("CADApplicationRuntime", text);
        };

        QString trialMsg = tr("Trial Version\n\n"
                              "You have %1 day(s) remaining in your trial period.\n\n"
                              "You can continue using the trial version or activate a license now.")
                               .arg(remainingDays);

        int trialChoice = -1;  // 0=Register, 1=Continue
        QMessageBox trialBox(QMessageBox::Information, tr("Trial Period"), trialMsg, QMessageBox::NoButton, nullptr);
        QPushButton* regBtn = trialBox.addButton(tr("Register Now"), QMessageBox::AcceptRole);
        QPushButton* contBtn = trialBox.addButton(tr("Continue Trial"), QMessageBox::RejectRole);
        QObject::connect(regBtn, &QPushButton::clicked, [&trialChoice]() {
            trialChoice = 0;
        });
        QObject::connect(contBtn, &QPushButton::clicked, [&trialChoice]() {
            trialChoice = 1;
        });
        trialBox.exec();

        SY_DEBUGF("[LicensingBootstrap] Trial dialog choice=%d", trialChoice);

        if (trialChoice == 0)
        {
            // 用户选择注册 → 激活成功则应用 License features，失败仍以试用模式继续
            LicenseDialog dlg(configDir);
            if (dlg.exec() == QDialog::Accepted)
            {
                SY_INFO("[LicensingBootstrap] Trial active: user activated license, upgrading to licensed mode");
                // 激活成功，继续执行下面的 License 检查来加载 features
            }
            else
            {
                SY_INFO("[LicensingBootstrap] Trial active: user cancelled activation, continuing in trial mode");
                inTrialMode = true;
            }
        }
        else
        {
            SY_INFO("[LicensingBootstrap] Trial active: user chose to continue trial");
            inTrialMode = true;
        }
    }
    else
    {
        // 试用期异常状态（天数为0但未过期）：按试用模式处理
        SY_WARN("[LicensingBootstrap] Trial check unexpected state, falling back to trial mode");
        inTrialMode = true;
        UiFeatureGate::instance().setUnrestricted(true);
    }

    // === Phase 2: 许可证复核（仅处理试用流程中用户点了注册的场景） ===
    if (inTrialMode)
    {
        return 0;
    }

    const LicenseProbe fresh = probeLicense(config);
    if (fresh.valid)
    {
        applyLicensedFeatures(fresh);
        return 0;
    }

    // 没有有效许可证且用户未在试用期流程中注册 → 弹出激活对话框
    SY_DEBUG("[LicensingBootstrap] License check failed, showing license dialog");
    LicenseDialog dlg(configDir);
    if (dlg.exec() != QDialog::Accepted)
    {
        SY_WARN("[LicensingBootstrap] License check rejected by user");
        return -3;
    }
    SY_DEBUG("[LicensingBootstrap] License accepted by user");

    // 激活成功后重新探测并应用 features
    const LicenseProbe activated = probeLicense(config);
    if (activated.valid)
    {
        applyLicensedFeatures(activated);
    }

    return 0;
}