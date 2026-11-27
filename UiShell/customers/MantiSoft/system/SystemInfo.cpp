#include "system/SystemInfo.h"

#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QSysInfo>

namespace
{
    // 执行一条命令并取标准输出；超时或失败返回空字符串。
    QString runCommand(const QString& program, const QStringList& args, int timeoutMs = 3000)
    {
        QProcess process;
        process.start(program, args);
        if (!process.waitForStarted(timeoutMs))
            return QString();
        if (!process.waitForFinished(timeoutMs))
        {
            process.kill();
            process.waitForFinished(1000);
            return QString();
        }
        if (process.exitCode() != 0)
            return QString();
        return QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
    }

    // 把字节数格式化成 "16.0GB"。
    QString formatMemoryBytes(qint64 bytes)
    {
        if (bytes <= 0)
            return QString();
        return QStringLiteral("%1GB").arg(static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0), 0, 'f', 1);
    }

    QString formatMemoryKb(qint64 kilobytes)
    {
        return formatMemoryBytes(kilobytes * 1024);
    }

#ifdef Q_OS_WIN
    // 读取注册表里的字符串值（Windows）。
    QString registryValue(const QString& key, const QString& name)
    {
        QSettings settings(key, QSettings::NativeFormat);
        return settings.value(name).toString().trimmed();
    }

    QString windowsOsVersion()
    {
        const QString key = QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion");
        const QString product = registryValue(key, QStringLiteral("ProductName"));
        const QString build = registryValue(key, QStringLiteral("CurrentBuildNumber"));
        const QString ubr = registryValue(key, QStringLiteral("UBR"));
        const QString display = registryValue(key, QStringLiteral("DisplayVersion"));

        QString version = product;
        if (!display.isEmpty())
            version += QStringLiteral(" ") + display;
        if (!build.isEmpty())
            version += QStringLiteral(" ") + build;
        if (!ubr.isEmpty())
            version += QStringLiteral(".") + ubr;
        return version;
    }

    QString windowsCpuName()
    {
        return registryValue(QStringLiteral("HKEY_LOCAL_MACHINE\\HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0"),
            QStringLiteral("ProcessorNameString"));
    }

    QString windowsRamInfo()
    {
        const QString output = runCommand(QStringLiteral("powershell.exe"),
            { QStringLiteral("-NoProfile"),
              QStringLiteral("-Command"),
              QStringLiteral("(Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory") });
        bool ok = false;
        const qint64 bytes = output.toLongLong(&ok);
        return ok ? formatMemoryBytes(bytes) : QString();
    }

    QString windowsGpuInfo()
    {
        const QString output = runCommand(QStringLiteral("powershell.exe"),
            { QStringLiteral("-NoProfile"),
              QStringLiteral("-Command"),
              QStringLiteral("(Get-CimInstance Win32_VideoController | Select-Object -First 1 -ExpandProperty Name)") });
        return output.simplified();
    }
#endif  // Q_OS_WIN

#ifdef Q_OS_MACOS
    QString macOsVersion()
    {
        const QString product = runCommand(QStringLiteral("/usr/bin/sw_vers"), { QStringLiteral("-productVersion") });
        const QString build = runCommand(QStringLiteral("/usr/bin/sw_vers"), { QStringLiteral("-buildVersion") });
        if (product.isEmpty())
            return QSysInfo::prettyProductName();
        return build.isEmpty() ? product : product + QStringLiteral(" (") + build + QLatin1Char(')');
    }

    QString macRamInfo()
    {
        const QString output = runCommand(QStringLiteral("/usr/sbin/sysctl"), { QStringLiteral("-n"), QStringLiteral("hw.memsize") });
        bool ok = false;
        const qint64 bytes = output.toLongLong(&ok);
        return ok ? formatMemoryBytes(bytes) : QString();
    }

    QString macCpuName()
    {
        return runCommand(QStringLiteral("/usr/sbin/sysctl"), { QStringLiteral("-n"), QStringLiteral("machdep.cpu.brand_string") });
    }

    QString macGpuInfo()
    {
        const QString output = runCommand(QStringLiteral("/usr/sbin/system_profiler"),
            { QStringLiteral("SPDisplaysDataType") });
        const QStringList lines = output.split(QLatin1Char('\n'));
        for (const QString& line : lines)
        {
            const QString trimmed = line.trimmed();
            if (trimmed.startsWith(QStringLiteral("Chipset Model:")))
                return trimmed.mid(QStringLiteral("Chipset Model:").length()).trimmed();
        }
        return QString();
    }
#endif  // Q_OS_MACOS

#ifdef Q_OS_LINUX
    QString linuxOsVersion()
    {
        QFile file(QStringLiteral("/etc/os-release"));
        if (file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            const QString content = QString::fromUtf8(file.readAll());
            const QStringList lines = content.split(QLatin1Char('\n'));
            for (const QString& line : lines)
            {
                if (line.startsWith(QStringLiteral("PRETTY_NAME=")))
                    return line.mid(QStringLiteral("PRETTY_NAME=").length()).remove(QLatin1Char('"')).trimmed();
            }
        }
        return QSysInfo::prettyProductName();
    }

    QString linuxRamInfo()
    {
        QFile file(QStringLiteral("/proc/meminfo"));
        if (file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            const QString content = QString::fromUtf8(file.readAll());
            const QStringList lines = content.split(QLatin1Char('\n'));
            for (const QString& line : lines)
            {
                if (line.startsWith(QStringLiteral("MemTotal:")))
                {
                    const QStringList parts = line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
                    if (parts.size() >= 2)
                        return formatMemoryKb(parts.at(1).toLongLong());
                }
            }
        }
        return QString();
    }

    QString linuxCpuName()
    {
        QFile file(QStringLiteral("/proc/cpuinfo"));
        if (file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            const QString content = QString::fromUtf8(file.readAll());
            const QStringList lines = content.split(QLatin1Char('\n'));
            for (const QString& line : lines)
            {
                if (line.startsWith(QStringLiteral("model name")))
                {
                    const int pos = line.indexOf(QLatin1Char(':'));
                    if (pos >= 0)
                        return line.mid(pos + 1).trimmed();
                }
            }
        }
        return QString();
    }

    QString linuxGpuInfo()
    {
        const QString output = runCommand(QStringLiteral("/usr/bin/lspci"), {});
        const QStringList lines = output.split(QLatin1Char('\n'));
        for (const QString& line : lines)
        {
            if (line.contains(QStringLiteral("VGA compatible controller"), Qt::CaseInsensitive)
                || line.contains(QStringLiteral("3D controller"), Qt::CaseInsensitive)
                || line.contains(QStringLiteral("Display controller"), Qt::CaseInsensitive))
            {
                const int pos = line.indexOf(QStringLiteral(": "));
                if (pos >= 0)
                    return line.mid(pos + 2).trimmed();
            }
        }
        return QString();
    }
#endif  // Q_OS_LINUX
}  // namespace

namespace MS
{
    SystemInfo SystemInfo::collect()
    {
        SystemInfo info;

#ifdef Q_OS_WIN
        info.osVersion = windowsOsVersion();
        info.ramInfo = windowsRamInfo();
        info.cpuInfo = windowsCpuName();
        info.gpuInfo = windowsGpuInfo();
#elif defined(Q_OS_MACOS)
        info.osVersion = macOsVersion();
        info.ramInfo = macRamInfo();
        info.cpuInfo = macCpuName();
        info.gpuInfo = macGpuInfo();
#elif defined(Q_OS_LINUX)
        info.osVersion = linuxOsVersion();
        info.ramInfo = linuxRamInfo();
        info.cpuInfo = linuxCpuName();
        info.gpuInfo = linuxGpuInfo();
#endif

        if (info.osVersion.isEmpty())
            info.osVersion = QSysInfo::prettyProductName();
        if (info.cpuInfo.isEmpty())
            info.cpuInfo = QSysInfo::currentCpuArchitecture();

        return info;
    }
}  // namespace MS