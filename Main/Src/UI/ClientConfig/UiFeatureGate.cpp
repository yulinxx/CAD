#include "UiFeatureGate.h"

#include "Log/SyLogger.h"

#include <QRegularExpression>
#include <algorithm>

UiFeatureGate& UiFeatureGate::instance()
{
    static UiFeatureGate gate;
    return gate;
}

void UiFeatureGate::loadFromLicenseString(const QString& featureCsv)
{
    // 分隔符统一处理：逗号 / 分号 / 空白，容忍现场签发时的书写差异
    static const QRegularExpression separators(QStringLiteral("[,;\\s]+"));
    const QStringList tokens = featureCsv.split(separators, Qt::SkipEmptyParts);
    setLicensedFeatures(tokens);
}

void UiFeatureGate::setLicensedFeatures(const QStringList& features)
{
    m_features.clear();
    bool wildcard = false;

    for (const QString& raw : features)
    {
        const QString id = raw.trimmed().toLower();
        if (id.isEmpty())
        {
            continue;
        }
        if (id == QLatin1String("*") || id == QLatin1String("all"))
        {
            wildcard = true;
            continue;
        }
        m_features.insert(id);
    }

    // 通配符授权等价于无限制模式，避免逐个 feature 维护白名单
    m_unrestricted = wildcard;

    if (m_unrestricted)
    {
        SY_INFO("[UiFeatureGate] Wildcard license detected, all features unlocked");
        return;
    }

    // 该日志是排查「某菜单项为什么不出现」的关键线索，务必保留
    SY_INFOF("[UiFeatureGate] Licensed features loaded (%d): %s",
        static_cast<int>(m_features.size()),
        qPrintable(licensedFeatures().join(QLatin1Char(','))));
}

void UiFeatureGate::setUnrestricted(bool unrestricted)
{
    m_unrestricted = unrestricted;
    SY_INFOF("[UiFeatureGate] Unrestricted mode %s", unrestricted ? "ON" : "OFF");
}

bool UiFeatureGate::isAllowed(const QString& featureId) const
{
    // 未声明 feature 的 UI 项属于基础功能，不受授权限制
    if (featureId.isEmpty())
    {
        return true;
    }
    if (m_unrestricted)
    {
        return true;
    }

    const bool allowed = m_features.contains(featureId.trimmed().toLower());
    if (!allowed)
    {
        SY_DEBUGF("[UiFeatureGate] Feature '%s' is not licensed, UI item will be hidden", qPrintable(featureId));
    }
    return allowed;
}

QStringList UiFeatureGate::licensedFeatures() const
{
    QStringList list(m_features.cbegin(), m_features.cend());
    std::sort(list.begin(), list.end());
    return list;
}

void UiFeatureGate::resetForTest()
{
    m_features.clear();
    m_unrestricted = true;
}
