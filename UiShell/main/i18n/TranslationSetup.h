#pragma once

#include <QString>

#include "UiShell/Contract/IApiObject.h"
#include "UiShell/i18n/IQtTextProvider.h"
#include "UiShell/i18n/ITranslationService.h"

// 翻译与文本查询
class TranslationSetup
{
public:
    // 创建翻译服务与文本提供者
    bool create();

    // 按命令行 --lang 与环境变量装载语言
    void installFromArguments();

    RD::ITranslationService* service() const
    {
        return m_transService.get();
    }

    RD::ITextProvider* textProvider() const
    {
        return m_text.get();
    }

    QString language() const;

private:
    // --lang <strId> 指定语言
    static QString requestedLanguage();

private:
    RD::ApiPtr<RD::ITranslationService> m_transService;
    RD::ApiPtr<RD::IQtTextProvider> m_text;
};
