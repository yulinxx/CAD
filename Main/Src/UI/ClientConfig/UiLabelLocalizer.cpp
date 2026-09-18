#include "UiLabelLocalizer.h"

#include <QByteArray>
#include <QCoreApplication>

QString uiLocalizedLabel(const QString& label, const QString& fallbackId)
{
    if (label.isEmpty())
    {
        return fallbackId;
    }

    // 优先从 WorkbenchMenuManager 上下文翻译（集中声明于
    // MenuLayoutTranslationStrings.cpp，覆盖全部 JSON 菜单/Dock 文案），
    // 再回退 MainWindow（命令目录文案）与 UiLayoutBuilder，兼容历史条目。
    // QByteArray 需持有到所有 translate 调用结束，否则 constData() 指针会悬垂
    const QByteArray sourceUtf8 = label.toUtf8();
    const char* source = sourceUtf8.constData();
    QString translated = QCoreApplication::translate("WorkbenchMenuManager", source);
    if (translated != label)
    {
        return translated;
    }
    translated = QCoreApplication::translate("MainWindow", source);
    if (translated != label)
    {
        return translated;
    }
    translated = QCoreApplication::translate("UiLayoutBuilder", source);
    if (translated != label)
    {
        return translated;
    }
    // 与原 actionLabel 行为一致：有英文源文案时始终返回源文案，不回退到 id
    return label;
}
