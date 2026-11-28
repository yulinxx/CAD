#include "Assembly/Dialogs/DialogController.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLatin1String>
#include <QString>

#include "UiShell/Assembly/ShellServices.h"
#include "UiShell/Contract/ITextProvider.h"
#include "UiShell/i18n/ITranslationService.h"

namespace AA
{
    namespace
    {
        const QLatin1String kKeySuffix("Key");
        const QLatin1String kPathSuffix("Path");

        // name 以 suffix 结尾且前面还有内容时返回前缀，否则返回空串。
        QString stripSuffix(const QString& name, QLatin1String suffix)
        {
            if (name.size() <= suffix.size() || !name.endsWith(suffix))
                return QString();
            return name.left(name.size() - suffix.size());
        }
    }  // namespace

    DialogController::DialogController(const ShellServices* services, const QString* profileDir)
        : m_shellServices(services)
        , m_strProfileDir(profileDir)
    {
    }

    DialogResult DialogController::openResult(const QString& dialogId, QWidget* parent, QString* lastMessage,
        const QJsonObject& params) const
    {
        IDialogFactory* factory = (m_shellServices && m_shellServices->dlgReg)
            ? m_shellServices->dlgReg->find(dialogId)
            : nullptr;

        if (!factory)
        {
            if (lastMessage)
                *lastMessage = QStringLiteral("no dialog factory registered for '%1'").arg(dialogId);
            return DialogResult{};
        }

        return factory->openDialog(parent, resolveParams(params));
    }

    QJsonObject DialogController::resolveParams(const QJsonObject& params) const
    {
        QJsonObject effective = params;

        // M5：profile 的 brand 段提供通用品牌参数；显式 dialog binding 优先。
        // logoPath 已在 ShellRuntime 中按 profile 目录解析成绝对路径。
        if (m_shellServices)
        {
            const QJsonObject& brandParams = m_shellServices->brandParams;
            for (auto it = brandParams.constBegin(); it != brandParams.constEnd(); ++it)
            {
                if (!effective.contains(it.key()))
                    effective.insert(it.key(), it.value());
            }
        }

        const ITextProvider* text = m_shellServices ? m_shellServices->text : nullptr;
        const QString profileDir = m_strProfileDir ? *m_strProfileDir : QString();

        for (auto it = params.constBegin(); it != params.constEnd(); ++it)
        {
            if (!it.value().isString())
                continue;
            const QString value = it.value().toString().trimmed();
            if (value.isEmpty())
                continue;

            // 1) <name>Key：翻译成功才覆盖 <name>；缺译（provider 返回 key 本身或空）时保留字面值。
            const QString target = stripSuffix(it.key(), kKeySuffix);
            if (!target.isEmpty())
            {
                if (text)
                {
                    const QString resolved = text->text(value).trimmed();
                    if (!resolved.isEmpty() && resolved != value)
                        effective.insert(target, resolved);
                }
                continue;
            }

            // 2) <name>Path：相对路径按 profile 所在目录解析；QFileInfo 把 ":/..." 视为绝对路径，qrc 路径不受影响。
            if (!stripSuffix(it.key(), kPathSuffix).isEmpty() && QFileInfo(value).isRelative() && !profileDir.isEmpty())
                effective.insert(it.key(), QDir(profileDir).absoluteFilePath(value));
        }

        // 3) 宿主上下文：不覆盖显式 params。
        const auto putContext = [&effective](const char* key, const QString& value) {
            const QString field = QString::fromLatin1(key);
            if (!value.isEmpty() && !effective.contains(field))
                effective.insert(field, value);
            };
        if (m_shellServices)
        {
            putContext(DialogParam::Theme, m_shellServices->strThemeId);
            putContext(DialogParam::Series, m_shellServices->strStyleSeries);
            if (m_shellServices->transService)
                putContext(DialogParam::Language, m_shellServices->transService->language());
        }
        putContext(DialogParam::AppName, QCoreApplication::applicationName());
        putContext(DialogParam::AppVersion, QCoreApplication::applicationVersion());

        return effective;
    }
}  // namespace AA