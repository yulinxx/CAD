/**
 * @file UiShortcutRegistry.cpp
 * @brief 快捷键台账与设置页模型实现
 */
#include "UiShortcutRegistry.h"

#include "UI/Shortcut/ShortcutOverrideStore.h"

#include "Log/SyLogger.h"

#include <QAction>
#include <QCoreApplication>
#include <QShortcut>

namespace
{
    constexpr const char* LOG_TAG = "UiShortcutRegistry";

    /// action id 的首段即分类（"file.exit" → "file"）；没有点号就整段当分类
    QString categoryFromActionId(const QString& actionId)
    {
        const int dot = actionId.indexOf(QLatin1Char('.'));
        return dot > 0 ? actionId.left(dot) : actionId;
    }

    QString portableText(const QKeySequence& key)
    {
        return key.isEmpty() ? QString() : key.toString(QKeySequence::PortableText);
    }

    /// 与 UiLayoutBuilder::actionLabel 同一条翻译回退链，保证设置页与菜单显示一致
    QString translateLabel(const QString& source)
    {
        if (source.isEmpty())
        {
            return source;
        }
        QString translated = QCoreApplication::translate("WorkbenchMenuManager", source.toUtf8().constData());
        if (translated != source)
        {
            return translated;
        }
        translated = QCoreApplication::translate("MainWindow", source.toUtf8().constData());
        if (translated != source)
        {
            return translated;
        }
        return QCoreApplication::translate("UiLayoutBuilder", source.toUtf8().constData());
    }
}  // namespace

void UiShortcutRegistry::beginBuild(const QString& scope)
{
    m_bindings.clear();

    if (scope != m_scope || m_overrides.isEmpty())
    {
        // 覆盖按作用域分文件：切工作台要换一份。同作用域重建（语言切换等）时
        // 沿用内存里的一份，避免把设置页刚落盘前的状态读回来覆盖掉。
        m_scope = scope;
        m_overrides = ShortcutOverrideStore::load(scope);
    }
}

QKeySequence UiShortcutRegistry::effectiveKey(const QString& commandId, const QKeySequence& configuredKey) const
{
    const auto it = m_overrides.constFind(commandId);
    if (it == m_overrides.constEnd())
    {
        return configuredKey;
    }
    // 覆盖值为空串是"用户清掉了这个快捷键"，要返回空序列而不是退回配置值。
    return it.value().isEmpty() ? QKeySequence() : QKeySequence(it.value(), QKeySequence::PortableText);
}

void UiShortcutRegistry::recordAction(QAction* action,
    const QString& commandId,
    const QString& actionId,
    const QString& displayName,
    const QKeySequence& defaultKey)
{
    if (!action || commandId.isEmpty())
    {
        return;
    }

    const int existing = indexOf(commandId);
    if (existing >= 0)
    {
        // 同一命令出现在多处菜单：只认第一处，否则设置页会出现重复行。
        SY_DEBUGF("[%s] Duplicated command '%s' in menus, keep the first binding", LOG_TAG, qPrintable(commandId));
        return;
    }

    Binding binding;
    binding.commandId = commandId;
    binding.actionId = actionId;
    binding.displayName = displayName.isEmpty() ? actionId : displayName;
    binding.category = categoryFromActionId(actionId);
    binding.defaultKey = defaultKey;
    binding.currentKey = action->shortcut();
    binding.action = action;
    m_bindings.push_back(binding);
}

void UiShortcutRegistry::recordShortcut(QShortcut* shortcut, const QString& commandId, const QKeySequence& defaultKey)
{
    if (!shortcut || commandId.isEmpty())
    {
        return;
    }

    const int existing = indexOf(commandId);
    if (existing >= 0)
    {
        // 命令已有菜单项绑定：QShortcut 只是同一命令的另一个入口，补上指针即可。
        m_bindings[existing].shortcut = shortcut;
        return;
    }

    Binding binding;
    binding.commandId = commandId;
    binding.actionId = commandId;
    binding.displayName = commandId;
    binding.category = categoryFromActionId(commandId);
    binding.defaultKey = defaultKey;
    binding.currentKey = shortcut->key();
    binding.shortcut = shortcut;
    m_bindings.push_back(binding);
}

int UiShortcutRegistry::indexOf(const QString& commandId) const
{
    for (int i = 0; i < m_bindings.size(); ++i)
    {
        if (m_bindings[i].commandId == commandId)
        {
            return i;
        }
    }
    return -1;
}

bool UiShortcutRegistry::setCurrentKey(const QString& commandId, const QKeySequence& key)
{
    const int index = indexOf(commandId);
    if (index < 0)
    {
        SY_WARNF("[%s] setCurrentKey: unknown command '%s'", LOG_TAG, qPrintable(commandId));
        return false;
    }
    m_bindings[index].currentKey = key;
    return true;
}

void UiShortcutRegistry::resetToDefault(const QString& commandId)
{
    const int index = indexOf(commandId);
    if (index >= 0)
    {
        m_bindings[index].currentKey = m_bindings[index].defaultKey;
    }
}

void UiShortcutRegistry::resetAllToDefaults()
{
    for (Binding& binding : m_bindings)
    {
        binding.currentKey = binding.defaultKey;
    }
}

QVector<QPair<QString, QString>> UiShortcutRegistry::conflicts() const
{
    QVector<QPair<QString, QString>> result;
    for (int i = 0; i < m_bindings.size(); ++i)
    {
        if (m_bindings[i].currentKey.isEmpty())
        {
            continue;
        }
        for (int j = i + 1; j < m_bindings.size(); ++j)
        {
            if (m_bindings[i].currentKey == m_bindings[j].currentKey)
            {
                result.append(qMakePair(m_bindings[i].commandId, m_bindings[j].commandId));
            }
        }
    }
    return result;
}

bool UiShortcutRegistry::applyAndPersist()
{
    for (const Binding& binding : m_bindings)
    {
        if (binding.currentKey == binding.defaultKey)
        {
            m_overrides.remove(binding.commandId);
        }
        else
        {
            m_overrides.insert(binding.commandId, portableText(binding.currentKey));
        }

        if (binding.action)
        {
            binding.action->setShortcut(binding.currentKey);
            if (!binding.currentKey.isEmpty())
            {
                // 与 UiLayoutBuilder::buildMenuItem 保持一致：浮动 Dock 是独立顶层窗口，
                // 默认的 WindowShortcut 在那里按不响应。
                binding.action->setShortcutContext(Qt::ApplicationShortcut);
            }
        }
        if (binding.shortcut)
        {
            binding.shortcut->setKey(binding.currentKey);
        }
    }

    return ShortcutOverrideStore::save(m_scope, m_overrides);
}

// ==================== UiShortcutSettingsModel ====================

UiShortcutSettingsModel::UiShortcutSettingsModel(UiShortcutRegistry* registry)
    : m_registry(registry)
{
}

QVector<IShortcutSettingsModel::Entry> UiShortcutSettingsModel::entries() const
{
    QVector<Entry> result;
    if (!m_registry)
    {
        return result;
    }

    const QVector<UiShortcutRegistry::Binding>& bindings = m_registry->bindings();
    result.reserve(bindings.size());
    for (const UiShortcutRegistry::Binding& binding : bindings)
    {
        Entry entry;
        entry.id = binding.commandId;
        entry.displayName = binding.displayName;
        entry.category = binding.category;
        entry.currentKey = binding.currentKey;
        entry.isConfigurable = true;
        result.append(entry);
    }
    return result;
}

QStringList UiShortcutSettingsModel::categories() const
{
    QStringList result;
    if (!m_registry)
    {
        return result;
    }
    for (const UiShortcutRegistry::Binding& binding : m_registry->bindings())
    {
        if (!binding.category.isEmpty() && !result.contains(binding.category))
        {
            result.append(binding.category);
        }
    }
    return result;
}

bool UiShortcutSettingsModel::setShortcut(const QString& id, const QKeySequence& key)
{
    return m_registry ? m_registry->setCurrentKey(id, key) : false;
}

void UiShortcutSettingsModel::resetToDefault(const QString& id)
{
    if (m_registry)
    {
        m_registry->resetToDefault(id);
    }
}

void UiShortcutSettingsModel::resetAllToDefaults()
{
    if (m_registry)
    {
        m_registry->resetAllToDefaults();
    }
}

QVector<QPair<QString, QString>> UiShortcutSettingsModel::checkConflicts() const
{
    return m_registry ? m_registry->conflicts() : QVector<QPair<QString, QString>>();
}

bool UiShortcutSettingsModel::persist()
{
    return m_registry ? m_registry->applyAndPersist() : false;
}

QString UiShortcutSettingsModel::localizedDisplayName(const QString& source) const
{
    return translateLabel(source);
}

QString UiShortcutSettingsModel::localizedCategory(const QString& source) const
{
    // 分类是 action id 的首段（file/edit/view/...），首字母大写后再走一遍菜单翻译链，
    // 正好命中顶层菜单标签（File/Edit/View）的翻译条目。
    if (source.isEmpty())
    {
        return source;
    }
    QString capitalized = source;
    capitalized[0] = capitalized[0].toUpper();
    return translateLabel(capitalized);
}
