#include "UiLabelLocalizer.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QCache>

// 翻译上下文优先级表（按查找顺序）
static const char* TRANSLATION_CONTEXTS[] = {
    "WorkbenchMenuManager",  // 菜单和Dock标题
    "MainWindow",           // 2D命令
    "MainWindow3D",         // 3D命令
    "UiLayoutBuilder",      // UI布局（历史兼容）
    "Navigation3D",          // 导航模式
    "Shortcuts"             // 快捷键
};
static constexpr int NUM_CONTEXTS = sizeof(TRANSLATION_CONTEXTS) / sizeof(TRANSLATION_CONTEXTS[0]);

// 翻译缓存 - 避免重复查询
// 注意：缓存key使用QLatin1String避免字符串拷贝
static QCache<QString, QString> s_translationCache(1024);

QString uiLocalizedLabel(const QString& label, const QString& fallbackId)
{
    if (label.isEmpty())
    {
        return fallbackId;
    }

    // 1. 先查缓存
    const QString* cached = s_translationCache.object(label);
    if (cached)
    {
        return *cached;
    }

    // 2. 依次查询各翻译上下文
    const QByteArray sourceUtf8 = label.toUtf8();
    const char* source = sourceUtf8.constData();

    for (int i = 0; i < NUM_CONTEXTS; ++i)
    {
        const QString translated = QCoreApplication::translate(TRANSLATION_CONTEXTS[i], source);
        if (translated != label)  // 找到有效翻译
        {
            // 缓存结果（仅缓存有翻译的条目）
            s_translationCache.insert(label, new QString(translated));
            return translated;
        }
    }

    // 3. 无翻译时返回 fallback 或原文
    const QString result = fallbackId.isEmpty() ? label : fallbackId;
    return result;
}
