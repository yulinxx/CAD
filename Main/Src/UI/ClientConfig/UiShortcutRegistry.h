#pragma once

/**
 * @file UiShortcutRegistry.h
 * @brief 配置驱动菜单的快捷键台账 + 设置页数据模型
 *
 * 背景：真正让 2D/3D 快捷键生效的链路只有一条 —— 客户 JSON → UiLayoutBuilder →
 * QAction（菜单项，ApplicationShortcut）/ QShortcut（shortcuts 节）。用户自定义要落到
 * 这条链上才有意义，所以台账在构建期记录每个命令的绑定对象，设置页直接改这些对象。
 *
 * 台账由 WorkbenchMenuManager 持有：UiLayoutBuilder 每次重建都被整体替换，
 * 而设置页可能在两次重建之间打开，指针寿命必须比 builder 长。
 */

#include "UI/Shortcut/IShortcutSettingsModel.h"

#include <QHash>
#include <QKeySequence>
#include <QPointer>
#include <QString>
#include <QVector>

class QAction;
class QShortcut;

/// 配置驱动菜单/快捷键的运行期台账
class UiShortcutRegistry
{
public:
    struct Binding
    {
        QString commandId;         ///< 唯一键，设置页 Entry::id 就是它
        QString actionId;          ///< JSON action id，用于推导分类
        QString displayName;       ///< 英文源文本（JSON label），翻译在展示时做
        QString category;          ///< 英文分类（actionId 的首段，如 file / edit）
        QKeySequence defaultKey;   ///< 配置里写的键（不含用户覆盖）
        QKeySequence currentKey;   ///< 当前值（含用户覆盖，或设置页里的未提交编辑）
        QPointer<QAction> action;  ///< 菜单项动作（shortcuts 节来的绑定为空）
        QPointer<QShortcut> shortcut;  ///< 全局 QShortcut（菜单项来的绑定为空）
    };

    /// 开始新一轮构建：清空台账；scope 变化时重新载入该作用域的用户覆盖
    /// @param scope 作用域，取当前工作台 id（"2d" / "3d"）
    void beginBuild(const QString& scope);

    const QString& scope() const
    {
        return m_scope;
    }

    /// 配置键叠加用户覆盖后实际应该生效的键；构建期由 UiLayoutBuilder 调用
    QKeySequence effectiveKey(const QString& commandId, const QKeySequence& configuredKey) const;

    /// 记录一个菜单项动作（无论是否配了快捷键都记，用户可以给空的分配一个）
    void recordAction(QAction* action,
        const QString& commandId,
        const QString& actionId,
        const QString& displayName,
        const QKeySequence& defaultKey);

    /// 记录一个由 shortcuts 节建出的全局快捷键
    void recordShortcut(QShortcut* shortcut, const QString& commandId, const QKeySequence& defaultKey);

    const QVector<Binding>& bindings() const
    {
        return m_bindings;
    }

    /// 改台账里的当前值（不动 QAction/QShortcut，OK 时才应用）
    bool setCurrentKey(const QString& commandId, const QKeySequence& key);
    void resetToDefault(const QString& commandId);
    void resetAllToDefaults();

    /// 把台账里的当前值写到 QAction/QShortcut 并把覆盖落盘
    bool applyAndPersist();

    /// 同键冲突（只看非空键），返回冲突的 commandId 对
    QVector<QPair<QString, QString>> conflicts() const;

private:
    int indexOf(const QString& commandId) const;

    QString m_scope;
    QVector<Binding> m_bindings;
    /// commandId → PortableText，来自用户覆盖文件；空字符串表示"用户主动清空"
    QHash<QString, QString> m_overrides;
};

/**
 * @brief 把 UiShortcutRegistry 适配成设置页的数据模型
 *
 * 编辑只落在台账上，OK（saveToData → persist）时才写 QAction/QShortcut 并落盘，
 * 取消就自然什么都没变。
 */
class UiShortcutSettingsModel : public IShortcutSettingsModel
{
public:
    explicit UiShortcutSettingsModel(UiShortcutRegistry* registry);

    QVector<Entry> entries() const override;
    QStringList categories() const override;

    bool setShortcut(const QString& id, const QKeySequence& key) override;
    void resetToDefault(const QString& id) override;
    void resetAllToDefaults() override;

    QVector<QPair<QString, QString>> checkConflicts() const override;
    bool persist() override;

    QString localizedDisplayName(const QString& source) const override;
    QString localizedCategory(const QString& source) const override;

private:
    UiShortcutRegistry* m_registry{ nullptr };
};
