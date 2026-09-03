#include "MachineProfile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QProcessEnvironment>

#include "Log/SyLogger.h"

namespace
{
    /// 档案文件的固定名字。放在应用配置目录下。
    const char* kProfileFileName = "machine.json";

    /// 现场调试/多机切换用的路径覆盖，与 SANYI_CLIENT_ID 同一套约定。
    const char* kProfileEnvKey = "SANYI_MACHINE_PROFILE";

    /// 兜底档案使用的模拟设备 ID（与 Hw::kSimulatedDeviceId 一致）。
    const char* kSimulatedDeviceId = "simulated_composite";

    MachineIoPointConfig parseIoPoint(const QJsonObject& obj)
    {
        MachineIoPointConfig p;
        p.name = obj.value("name").toString();
        p.label = obj.value("label").toString();
        p.channel = obj.value("channel").toInt(-1);
        // direction/signal 用字符串而不是布尔：JSON 里 "direction": "output"
        // 比 "output": true 更容易读懂，配错的概率也更低
        p.output = obj.value("direction").toString("input").compare("output", Qt::CaseInsensitive) == 0;
        p.analog = obj.value("signal").toString("digital").compare("analog", Qt::CaseInsensitive) == 0;
        p.activeLow = obj.value("activeLow").toBool(false);
        p.debounceMs = obj.value("debounceMs").toInt(0);
        p.analogThreshold = obj.value("analogThreshold").toDouble(0.0);
        p.analogActiveBelow = obj.value("analogActiveBelow").toBool(true);
        return p;
    }

    MachineSafetyConditionConfig parseSafetyCondition(const QJsonObject& obj)
    {
        MachineSafetyConditionConfig c;
        c.pointName = obj.value("point").toString();
        c.description = obj.value("description").toString();
        c.triggerOnActive = obj.value("triggerOnActive").toBool(true);
        c.severity = obj.value("severity").toString("blocked");
        const QJsonArray actions = obj.value("actions").toArray();
        for (const QJsonValue& v : actions)
        {
            c.actions.append(v.toString());
        }
        // 缺省 fail-safe：没写就是「读不到当违规」
        c.violateWhenInvalid = obj.value("violateWhenInvalid").toBool(true);
        return c;
    }
}

namespace MachineProfileLoader
{
    QString resolvePath(const QString& configDir)
    {
        const QString envPath =
            QProcessEnvironment::systemEnvironment().value(QString::fromLatin1(kProfileEnvKey));
        if (!envPath.isEmpty())
        {
            if (QFileInfo::exists(envPath))
            {
                return envPath;
            }
            // 显式指定了却不存在必须报出来：这几乎总是打错了路径，
            // 静默退回默认位置会让人以为环境变量生效了
            SY_WARNF("[MachineProfile] %s points to a missing file: %s",
                kProfileEnvKey, envPath.toUtf8().constData());
        }

        if (!configDir.isEmpty())
        {
            const QString candidate = QDir(configDir).filePath(QString::fromLatin1(kProfileFileName));
            if (QFileInfo::exists(candidate))
            {
                return candidate;
            }
        }
        return QString();
    }

    bool loadFromFile(const QString& path, MachineProfile& profileOut, QString& errorOut)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
        {
            errorOut = QStringLiteral("无法打开机器档案：%1").arg(path);
            return false;
        }

        QJsonParseError parseError{};
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
        file.close();

        if (parseError.error != QJsonParseError::NoError)
        {
            errorOut = QStringLiteral("机器档案 JSON 解析失败（偏移 %1）：%2")
                           .arg(parseError.offset)
                           .arg(parseError.errorString());
            return false;
        }
        if (!doc.isObject())
        {
            errorOut = QStringLiteral("机器档案根节点必须是对象：%1").arg(path);
            return false;
        }

        const QJsonObject root = doc.object();

        MachineProfile profile;
        profile.deviceId = root.value("deviceId").toString();
        if (profile.deviceId.isEmpty())
        {
            errorOut = QStringLiteral("机器档案缺少 deviceId：%1").arg(path);
            return false;
        }

        // open 参数：值一律转成字符串，与 Hw::ParamValue 的约定一致
        // （HAL 侧刻意只用字符串承载，由适配器自己解析类型）
        const QJsonObject params = root.value("openParams").toObject();
        for (auto it = params.constBegin(); it != params.constEnd(); ++it)
        {
            const QJsonValue& v = it.value();
            QString text;
            if (v.isBool())
            {
                text = v.toBool() ? QStringLiteral("1") : QStringLiteral("0");
            }
            else if (v.isDouble())
            {
                text = QString::number(v.toDouble(), 'g', 12);
            }
            else
            {
                text = v.toString();
            }
            profile.openParams.insert(it.key(), text);
        }

        const QJsonArray points = root.value("ioPoints").toArray();
        for (const QJsonValue& v : points)
        {
            const MachineIoPointConfig p = parseIoPoint(v.toObject());
            if (p.name.isEmpty() || p.channel < 0)
            {
                // 单条点位配错不该让整台机器起不来，但必须留下明确日志：
                // 少一个点位会让依赖它的安全条件判定为 invalid，
                // 而 violateWhenInvalid 默认为 true，最终表现为「无法开工」——
                // 这条日志就是把「无法开工」和「配置写错」连起来的唯一线索
                SY_ERRORF("[MachineProfile] Skipped invalid IO point: name='%s' channel=%d",
                    p.name.toUtf8().constData(), p.channel);
                continue;
            }
            profile.ioPoints.append(p);
        }

        const QJsonArray conditions = root.value("safetyConditions").toArray();
        for (const QJsonValue& v : conditions)
        {
            const MachineSafetyConditionConfig c = parseSafetyCondition(v.toObject());
            if (c.pointName.isEmpty())
            {
                SY_ERRORF("[MachineProfile] Skipped safety condition without point name");
                continue;
            }
            profile.safetyConditions.append(c);
        }

        profile.tickIntervalMs = root.value("tickIntervalMs").toInt(20);
        if (profile.tickIntervalMs < 1 || profile.tickIntervalMs > 1000)
        {
            SY_WARNF("[MachineProfile] tickIntervalMs=%d out of range, using 20ms",
                profile.tickIntervalMs);
            profile.tickIntervalMs = 20;
        }
        profile.autoOpen = root.value("autoOpen").toBool(true);
        profile.sourcePath = path;
        profile.fromFallback = false;

        profileOut = profile;
        return true;
    }

    MachineProfile builtinSimulatedProfile()
    {
        MachineProfile profile;
        profile.deviceId = QString::fromLatin1(kSimulatedDeviceId);
        profile.fromFallback = true;

        profile.openParams.insert(QStringLiteral("axis_count"), QStringLiteral("3"));
        profile.openParams.insert(QStringLiteral("field_size_mm"), QStringLiteral("110"));
        profile.openParams.insert(QStringLiteral("rated_power_watt"), QStringLiteral("30"));
        profile.openParams.insert(QStringLiteral("laser_type"), QStringLiteral("Fiber"));

        // 兜底档案照样配安全点位：模拟模式下安全链路必须是「真的在跑」，
        // 否则上层的联锁逻辑到了真机才第一次被执行
        MachineIoPointConfig estop;
        estop.name = QStringLiteral("safety.estop");
        estop.label = QStringLiteral("Emergency Stop");  // 急停
        estop.channel = 0;

        // 急停按常闭接法：**高电平 = 正常，低电平 = 被按下**，
        // 所以「active（被按下）」对应低电平，activeLow 必须为 true。
        // 写成 false 的后果是模拟机一上电就被判成「急停被按下」而永远无法开工 ——
        // 而「开发机能跑模拟」恰恰是兜底档案存在的唯一意义。
        // 这个极性必须与 SimulatedDevice 里 DI0 的约定一致（高=正常）。
        estop.activeLow = true;
        estop.debounceMs = 0;  // 安全信号不去抖
        profile.ioPoints.append(estop);

        MachineIoPointConfig door;
        door.name = QStringLiteral("safety.door_closed");
        door.label = QStringLiteral("Safety Door");  // 安全门
        door.channel = 1;

        // 门磁同样常闭（高=门已关），但这里点位的语义就是「active = 门已关」，
        // 因此极性不翻转；「没关才是违规」由下面的 triggerOnActive=false 表达。
        door.activeLow = false;
        door.debounceMs = 20;
        profile.ioPoints.append(door);


        MachineSafetyConditionConfig estopCond;
        estopCond.pointName = estop.name;
        estopCond.description = QStringLiteral("Emergency stop pressed");  // 急停被按下
        estopCond.triggerOnActive = true;
        estopCond.severity = QStringLiteral("emergency");
        estopCond.actions << QStringLiteral("emergency");
        profile.safetyConditions.append(estopCond);

        MachineSafetyConditionConfig doorCond;
        doorCond.pointName = door.name;
        doorCond.description = QStringLiteral("Safety door not closed");  // 安全门未关闭
        doorCond.triggerOnActive = false;  // active 表示「门已关」，未 active 才是违规
        doorCond.severity = QStringLiteral("blocked");
        doorCond.actions << QStringLiteral("block_start") << QStringLiteral("laser_off");
        profile.safetyConditions.append(doorCond);

        return profile;
    }

    MachineProfile loadOrFallback(const QString& configDir, QString& warningOut)
    {
        const QString path = resolvePath(configDir);
        if (path.isEmpty())
        {
            warningOut = QStringLiteral(
                "未找到机器档案（%1 或 %2），已进入模拟设备模式：当前不会驱动任何真实硬件。")
                             .arg(QString::fromLatin1(kProfileEnvKey))
                             .arg(QDir(configDir).filePath(QString::fromLatin1(kProfileFileName)));
            // 日志走英文：warningOut 是给界面看的中文提示，而日志会流向控制台与
            // 现场日志文件，那里的编码不受我们控制，中文常出现 mojibake。
            SY_WARNF("[MachineProfile] no machine profile found (env %s or file %s), "
                     "falling back to simulated device mode: no real hardware will be driven",
                kProfileEnvKey,
                QDir(configDir).filePath(QString::fromLatin1(kProfileFileName)).toUtf8().constData());
            return builtinSimulatedProfile();
        }

        MachineProfile profile;
        QString error;
        if (!loadFromFile(path, profile, error))
        {
            // 解析失败仍然回退，但把原因升级为 ERROR 并原样交给调用方展示：
            // 「配置写错了」和「这台机器没配硬件」是两件事，日志必须区分
            warningOut = QStringLiteral("%1；已临时进入模拟设备模式。").arg(error);
            SY_ERRORF("[MachineProfile] %s", warningOut.toUtf8().constData());
            return builtinSimulatedProfile();
        }

        SY_INFOF("[MachineProfile] Loaded '%s' from %s (%lld IO point(s), %lld safety condition(s))",
            profile.deviceId.toUtf8().constData(),
            path.toUtf8().constData(),
            static_cast<long long>(profile.ioPoints.size()),
            static_cast<long long>(profile.safetyConditions.size()));
        return profile;
    }
}
