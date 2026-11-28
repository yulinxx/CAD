#include "CustomerModuleProvider.h"

#include <QLibraryInfo>
#include <QString>

#include "UiShell/Contract/BuildFingerprint.h"

namespace
{
    // 检查 Qt 运行时与编译期版本是否一致
    bool checkQtRuntime(QString& error)
    {
        const QString runtimeVersion = QString::fromLatin1(qVersion());
        const QString buildVersion = QString::fromLatin1(QT_VERSION_STR);

        if (runtimeVersion == buildVersion)
            return true;

        error = QStringLiteral("Qt runtime %1 does not match build %2 (libraries: %3)")
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
            .arg(runtimeVersion, buildVersion, QLibraryInfo::path(QLibraryInfo::LibrariesPath));
#else
            .arg(runtimeVersion, buildVersion, QLibraryInfo::location(QLibraryInfo::LibrariesPath));
#endif

        return false;
    }
}  // namespace

bool CustomerModuleProvider::load()
{
    m_error.clear();

    if (!checkQtRuntime(m_error))
    {
        m_module = AA::CustomerModule();
        return false;
    }

    const QString hostFP = QString::fromLatin1(AA::hostBuildFingerprint());
    const QString customerFP = QString::fromLatin1(AA::customerBuildFingerprint());
    if (customerFP != hostFP)
    {
        m_error = QStringLiteral("Tyvok Studiouild fingerprint mismatch\n  host: %1\n  dll : %2").arg(hostFP, customerFP);

        m_module = AA::CustomerModule();
        return false;
    }

    m_module = AA::createCustomerModule();

    // 接管
    m_owner.reset(m_module.owner);
    m_module.owner = nullptr;
    return true;
}

QString CustomerModuleProvider::title() const
{
    return titleFor(m_module);
}

QString CustomerModuleProvider::titleFor(const AA::CustomerModule& module)
{
    // 窗口标题只显示客户工程名（MantiSoft / GoodLaser / TyvokStudio）
    return module.displayNameFn ? module.displayNameFn() : module.strDisplayName;
}