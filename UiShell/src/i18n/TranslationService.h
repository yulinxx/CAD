#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include "UiShell/i18n/ITranslationService.h"

class QTranslator;

namespace AA
{
    // 基于 Qt 原生 QTranslator 的翻译装载器：只负责 .qm 的装载/卸载与回退，
    // 不保存任何 key → 文案的映射（文案全部在每个 DLL 自己的 .ts 里）。
    //
    // 多 DLL 约定：
    //   - 每个 UI DLL 带自己的翻译文件，命名跟库走：UiShell_zh_CN.qm、
    //     UiMantiSoft_zh_CN.qm ……；.qm 由各自的 .ts 生成。
    //   - 加载与切换只在宿主这一处（本类）完成，DLL 内部不 installTranslator。
    //   - 同一语言下按目录内实际存在的 *_<language>.qm 逐个装载，
    //     因此新增客户 DLL 不需要改宿主代码，也不会漏掉某个 DLL 的文案。
    //
    // 源语言是英文：代码里的可翻译文本一律写成英文 source，由 .ts 翻译成目标语言；
    // 缺条目时 Qt 直接返回 source，即英文原文。profile 的稳定 key（menu.file）
    // 在英文下同样需要 .qm，因此每个模块都保留 en_US 的 .ts。
    //
    // 具体类不导出：调用方一律使用 createTranslationService() 返回的 ITranslationService*。
    class TranslationService final : public ITranslationService
    {
    public:
        TranslationService();
        ~TranslationService() override;

        TranslationService(const TranslationService&) = delete;
        TranslationService& operator=(const TranslationService&) = delete;

    public:
        // 框架内置语言（顺序稳定，英文在首位作为默认）。
        QStringList builtinLanguages() const override;

        // "en" / "en-US" / "EN_us" → en_US；"zh-Hans" / "zh_TW" → zh_CN；
        // ja / ko / fr / de / es / pt / ru / ar / it / hi / tr / vi 同理；
        // 空 → 空（表示“未指定，跟随系统”）；其他语言原样保留。
        QString normalizeLanguageId(const QString& id) const override;

        // 是否为内置支持的语言（归一化后比较）。
        bool isSupported(const QString& lang) const override;

        // 系统语言：受支持时使用系统语言，否则回退英文。
        QString defaultLanguage() const override;

        // 运行期翻译目录：<可执行文件目录>/translations。
        QString defaultDirectory() const override;

        // 安装语言；language 为空用 defaultLanguage()，directory 为空用 defaultDirectory()
        bool install(const QString& lang, const QString& dir = QString()) override;

        // 卸载全部 translator，并释放内部 QTranslator；可重复调用。
        void uninstall() override;

        bool isInstalled() const override;
        // 实际生效的语言：目标 .qm 缺失时为 en_US。
        QString language() const override;
        // 调用方请求的语言（归一化后）。
        QString requestedLanguage() const override;
        QString lastError() const override;
        QString directory() const override;

        void destroy() override
        {
            delete this;
        }

    private:
        // 装载某个语言下所有模块 .qm；shell 必须存在，其余尽力装载
        int loadLanguage(const QString& lang);
        // 只按给定文件名后缀装载；loadLanguage() 会再尝试该语言的短码别名。
        int loadLanguageFiles(const QString& lang);

        // 装载单个 .qm 文件；失败返回 false 且不改变已装载列表。
        bool loadFile(const QString& path);

    private:
        QList<QTranslator*> m_translators;
        QString m_requested;
        QString m_language;
        QString m_directory;
        QString m_lastError;
        QStringList m_loadedFiles;
    };
}  // namespace AA
