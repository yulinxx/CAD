#include "TranslationService.h"

#include <QCoreApplication>
#include <QDir>
#include <QHash>
#include <QLocale>
#include <QStringList>
#include <QTranslator>

namespace AA
{
    namespace
    {
        const QString kEnUs = QStringLiteral("en_US");
        const QString kZhCn = QStringLiteral("zh_CN");
        const QString kJaJp = QStringLiteral("ja_JP");
        const QString kKoKr = QStringLiteral("ko_KR");
        const QString kRuRu = QStringLiteral("ru_RU");
        const QString kDeDe = QStringLiteral("de_DE");
        const QString kFrFr = QStringLiteral("fr_FR");
        const QString kEsEs = QStringLiteral("es_ES");
        const QString kPtPt = QStringLiteral("pt_PT");
        const QString kArAr = QStringLiteral("ar_AR");
        const QString kItIt = QStringLiteral("it_IT");
        const QString kHiIn = QStringLiteral("hi_IN");
        const QString kTrTr = QStringLiteral("tr_TR");
        const QString kViVn = QStringLiteral("vi_VN");

        // 框架 DLL 的翻译文件固定叫 UiShell_<language>.qm。
        const QString kShellModule = QStringLiteral("UiShell");

        QString qmFileName(const QString& module, const QString& language)
        {
            return module + QLatin1Char('_') + language + QStringLiteral(".qm");
        }

        QString moduleFromFileName(const QString& fileName, const QString& language)
        {
            const QString suffix = QLatin1Char('_') + language + QStringLiteral(".qm");
            if (!fileName.endsWith(suffix))
                return QString();

            return fileName.left(fileName.size() - suffix.size());
        }

        // 历史客户 .qm 用短码命名（April_zh.qm）；规范化后的 locale id 仍优先，
        // 短码只在别名阶段补装，避免为一个客户把命名规则写进通用加载器。
        QStringList shortCodeAliases(const QString& language)
        {
            static const QHash<QString, QStringList> aliases{
                { kZhCn, QStringList{ QStringLiteral("zh") } },
                { kJaJp, QStringList{ QStringLiteral("ja") } },
                { kKoKr, QStringList{ QStringLiteral("ko") } },
                { kRuRu, QStringList{ QStringLiteral("ru") } },
                { kDeDe, QStringList{ QStringLiteral("de") } },
                { kFrFr, QStringList{ QStringLiteral("fr") } },
                { kEsEs, QStringList{ QStringLiteral("es") } },
                { kPtPt, QStringList{ QStringLiteral("pt") } },
                { kArAr, QStringList{ QStringLiteral("ar") } },
                { kItIt, QStringList{ QStringLiteral("it") } },
                { kHiIn, QStringList{ QStringLiteral("hi") } },
                { kTrTr, QStringList{ QStringLiteral("tr") } },
                { kViVn, QStringList{ QStringLiteral("vi") } },
            };
            return aliases.value(language);
        }
    }  // namespace

    TranslationService::TranslationService()
    {
    }

    TranslationService::~TranslationService()
    {
        uninstall();
    }

    QStringList TranslationService::builtinLanguages() const
    {
        // 英文是源语言，放在首位作为默认与最终回退。
        return { kEnUs, kZhCn, kJaJp, kKoKr, kFrFr, kDeDe, kEsEs, kPtPt,
            kRuRu, kArAr, kItIt, kHiIn, kTrTr, kViVn };
    }

    QString TranslationService::normalizeLanguageId(const QString& id) const
    {
        const QString trimmed = id.trimmed();
        // 空 strId 表示“未指定语言”，由 defaultLanguage() 决定跟随系统。
        if (trimmed.isEmpty())
            return QString();

        QString normalized = trimmed;
        // "en-US" 与 "en_US" 是同一种语言，先统一分隔符再比较。
        normalized.replace(QLatin1Char('-'), QLatin1Char('_'));
        const QString lower = normalized.toLower();
        // 只按语言前缀匹配，地区差异（zh_TW、en_GB、de_AT）统一归到内置翻译。
        if (lower.startsWith(QLatin1String("en")))
            return kEnUs;
        if (lower.startsWith(QLatin1String("zh")))
            return kZhCn;
        if (lower.startsWith(QLatin1String("ja")))
            return kJaJp;
        if (lower.startsWith(QLatin1String("ko")))
            return kKoKr;
        if (lower.startsWith(QLatin1String("fr")))
            return kFrFr;
        if (lower.startsWith(QLatin1String("de")))
            return kDeDe;
        if (lower.startsWith(QLatin1String("es")))
            return kEsEs;
        if (lower.startsWith(QLatin1String("pt")))
            return kPtPt;
        if (lower.startsWith(QLatin1String("ru")))
            return kRuRu;
        if (lower.startsWith(QLatin1String("ar")))
            return kArAr;
        if (lower.startsWith(QLatin1String("it")))
            return kItIt;
        if (lower.startsWith(QLatin1String("hi")))
            return kHiIn;
        if (lower.startsWith(QLatin1String("tr")))
            return kTrTr;
        if (lower.startsWith(QLatin1String("vi")))
            return kViVn;
        return trimmed;
    }

    bool TranslationService::isSupported(const QString& lang) const
    {
        return builtinLanguages().contains(lang);
    }

    QString TranslationService::defaultLanguage() const
    {
        // 系统语言受支持时直接使用；不在支持列表里则回退英文。
        const QString system = normalizeLanguageId(QLocale::system().name());
        return isSupported(system) ? system : kEnUs;
    }

    QString TranslationService::defaultDirectory() const
    {
        return QDir::cleanPath(QCoreApplication::applicationDirPath() + QStringLiteral("/translations"));
    }

    bool TranslationService::install(const QString& lang, const QString& dir)
    {
        // 先彻底卸下旧 translator，保证运行期切换不会叠加旧语言的条目。
        uninstall();

        // 未指定语言时跟随系统；传入未知语言时保留原样以便在错误里说明。
        m_requested = lang.trimmed().isEmpty() ? defaultLanguage() : normalizeLanguageId(lang);
        m_directory = dir.trimmed().isEmpty() ? defaultDirectory() : QDir::cleanPath(dir);
        m_language.clear();
        m_lastError.clear();

        if (!isSupported(m_requested))
        {
            // 不支持的语言回退英文；尽力装载英文包，保证 profile key 也有可读文案。
            loadLanguage(kEnUs);
            m_language = kEnUs;
            m_lastError = QStringLiteral("Unsupported language '%1'; falling back to en_US.").arg(m_requested);
            return false;
        }

        const int loaded = loadLanguage(m_requested);
        if (loaded > 0)
        {
            m_language = m_requested;
            return true;
        }

        // 目标语言完全缺失时回退英文；英文自身缺失仍可显示代码里的英文原文。
        if (m_requested != kEnUs)
            loadLanguage(kEnUs);

        m_language = kEnUs;

        if (m_requested == kEnUs)
            return true;

        m_lastError = QStringLiteral("Translation file for %1 not found: %2/%3; falling back to en_US.")
            .arg(m_requested, m_directory, qmFileName(kShellModule, m_requested));

        return false;
    }

    int TranslationService::loadLanguage(const QString& lang)
    {
        // 规范化文件名优先；历史短码文件名作为别名补装。
        int loaded = loadLanguageFiles(lang);
        for (const QString& alias : shortCodeAliases(lang))
            loaded += loadLanguageFiles(alias);
        return loaded;
    }

    int TranslationService::loadLanguageFiles(const QString& lang)
    {
        int loaded = 0;

        // shell 自己的 .qm 必须先装上：菜单/标题栏/关于框都靠它。
        if (loadFile(m_directory + QLatin1Char('/') + qmFileName(kShellModule, lang)))
            ++loaded;

        // 其余 DLL 的翻译按目录里实际存在的文件装载：
        // 新增客户 DLL 只要把 <Target>_<language>.qm 放进同一目录，就自动生效。
        const QStringList filters{ QStringLiteral("*_") + lang + QStringLiteral(".qm") };
        const QStringList files = QDir(m_directory).entryList(filters, QDir::Files, QDir::Name);

        for (const QString& fileName : files)
        {
            const QString module = moduleFromFileName(fileName, lang);
            if (module.isEmpty() || module == kShellModule)
                continue;

            if (loadFile(m_directory + QLatin1Char('/') + fileName))
                ++loaded;
        }

        return loaded;
    }

    bool TranslationService::loadFile(const QString& path)
    {
        // 同一个文件可能同时匹配规范化名与短码别名；只装一次，避免重复 translator。
        if (m_loadedFiles.contains(path))
            return false;

        auto* translator = new QTranslator;
        if (!translator->load(path))
        {
            delete translator;
            return false;
        }
        QCoreApplication::installTranslator(translator);
        m_translators.append(translator);
        m_loadedFiles.append(path);
        return true;
    }

    void TranslationService::uninstall()
    {
        for (QTranslator* translator : m_translators)
        {
            QCoreApplication::removeTranslator(translator);
            delete translator;
        }
        m_translators.clear();
        m_loadedFiles.clear();
        m_language.clear();
    }

    bool TranslationService::isInstalled() const
    {
        return !m_translators.isEmpty();
    }

    QString TranslationService::language() const
    {
        return m_language;
    }

    QString TranslationService::requestedLanguage() const
    {
        return m_requested;
    }

    QString TranslationService::lastError() const
    {
        return m_lastError;
    }

    QString TranslationService::directory() const
    {
        return m_directory;
    }

    ITranslationService* createTranslationService()
    {
        return new TranslationService;
    }
}  // namespace AA