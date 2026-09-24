#include "UiLabelLocalizer.h"

#include <QByteArray>
#include <QCoreApplication>

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

QString uiLocalizedLabel(const QString& label, const QString& fallbackId)
{
    if (label.isEmpty())
    {
        return fallbackId;
    }

    // 1. 先查缓存
    QCache<QString, QString>& cache = getTranslationCache();
    const QString* cached = cache.object(label);
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
            cache.insert(label, new QString(translated));
            return translated;
        }
    }

    // 3. 无翻译时返回原始 label
    return label;
}
