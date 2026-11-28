#include "TranslationSetup.h"

#include <QCoreApplication>

#include <cstdio>

bool TranslationSetup::create()
{
    m_transService.reset(AA::createTranslationService());
    if (!m_transService)
        return false;

    m_text.reset(AA::createQtTextProvider());
    return static_cast<bool>(m_text);
}

QString TranslationSetup::requestedLanguage()
{
    const QStringList args = QCoreApplication::arguments();
    for (int i = 1; i + 1 < args.size(); ++i)
    {
        if (args.at(i) == QLatin1String("--lang"))
            return args.at(i + 1);
    }
    return QString();
}

void TranslationSetup::installFromArguments()
{
    if (!m_transService)
        return;

    if (!m_transService->install(requestedLanguage()))
        std::fprintf(stderr, "App: %s\n", qPrintable(m_transService->lastError()));

    std::fprintf(stderr, "App: ui language = %s\n", qPrintable(m_transService->language()));
}

QString TranslationSetup::language() const
{
    return m_transService ? m_transService->language() : QString();
}