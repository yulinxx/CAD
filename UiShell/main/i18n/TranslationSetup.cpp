#include "TranslationSetup.h"

#include <QCoreApplication>

#include "UiShell/i18n/LanguageArgument.h"

#include <cstdio>

bool TranslationSetup::create()
{
    m_transService.reset(AA::createTranslationService());
    if (!m_transService)
        return false;

    m_text.reset(AA::createQtTextProvider());
    return static_cast<bool>(m_text);
}

void TranslationSetup::installFromArguments()
{
    if (!m_transService)
        return;

    if (!m_transService->install(AA::requestedLanguageArgument()))
        std::fprintf(stderr, "App: %s\n", qPrintable(m_transService->lastError()));

    std::fprintf(stderr, "App: ui language = %s\n", qPrintable(m_transService->language()));
}

QString TranslationSetup::language() const
{
    return m_transService ? m_transService->language() : QString();
}