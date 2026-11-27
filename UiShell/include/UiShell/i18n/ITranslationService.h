#pragma once

#include <QString>
#include <QStringList>

#include "UiShell/Export.h"
#include "UiShell/Contract/IApiObject.h"

namespace RD
{
    class ITranslationService : public IApiObject
    {
    public:
        virtual QStringList builtinLanguages() const = 0;
        virtual bool isSupported(const QString& language) const = 0;
        virtual QString defaultLanguage() const = 0;
        virtual QString normalizeLanguageId(const QString& id) const = 0;
        virtual QString defaultDirectory() const = 0;

        virtual bool install(const QString& language, const QString& directory = QString()) = 0;
        virtual void uninstall() = 0;

        virtual bool isInstalled() const = 0;
        virtual QString language() const = 0;
        virtual QString requestedLanguage() const = 0;
        virtual QString lastError() const = 0;
        virtual QString directory() const = 0;

    protected:
        // 只能经 destroy()释放，禁止跨模块直接 delete
        ~ITranslationService() override = default;
    };

    UI_SHELL_API ITranslationService* createTranslationService();
}  // namespace RD
