#include "QtTextProvider.h"

#include <QByteArray>
#include <QCoreApplication>

#include "i18n/TextContext.h"

namespace AA
{
    QString QtTextProvider::text(const QString& key) const
    {
        const QByteArray context = textContextForKey(key).toUtf8();
        const QByteArray source = key.toUtf8();
        const QString translated = QCoreApplication::translate(context.constData(), source.constData());

        return translated.isEmpty() ? key : translated;
    }

    IQtTextProvider* createQtTextProvider()
    {
        return new QtTextProvider;
    }
}  // namespace AA