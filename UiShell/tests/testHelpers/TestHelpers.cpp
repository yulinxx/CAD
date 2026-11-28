#include "TestHelpers.h"

#include <QJsonDocument>
#include <QLabel>
#include <QWidget>

namespace AA::test
{
    void FakeCommandInvoker::invoke(const QString& commandId)
    {
        invoked.append(commandId);
    }

    QWidget* FakeWidgetFactory::create(const PanelCreateRequest& info)
    {
        ++created;
        auto* label = new QLabel(info.strPanelId, info.parent);
        label->setObjectName(QStringLiteral("panel:") + info.strPanelId);
        return label;
    }

    void FakeWidgetFactoryRegistry::registerFactory(const QString& factoryId,
        IWidgetFactory* factory)
    {
        if (factoryId.isEmpty() || !factory)
            return;
        m_factories.insert(factoryId, factory);
    }

    IWidgetFactory* FakeWidgetFactoryRegistry::find(const QString& factoryId) const
    {
        return m_factories.value(factoryId, nullptr);
    }

    QWidget* FakeCenterViewProvider::createCenterView(QWidget* parent)
    {
        auto* widget = new QLabel(QStringLiteral("center"), parent);
        widget->setObjectName(QStringLiteral("center.view"));
        return widget;
    }

    void FakeTitleBarHost::requestDrag()
    {
        ++dragCount;
    }

    void FakeTitleBarHost::requestMinimize()
    {
        ++minimizeCount;
    }

    void FakeTitleBarHost::requestToggleMaximize()
    {
        ++toggleMaximizeCount;
    }

    void FakeTitleBarHost::requestClose()
    {
        ++closeCount;
    }

    bool FakeTitleBarHost::isMaximized() const
    {
        return maximized;
    }

    QByteArray makeProfileJson(const QString& profileId,
        const QJsonArray& regions,
        const QJsonObject& titleBar,
        const QJsonArray& menus,
        const QJsonArray& panels)
    {
        QJsonObject root;
        root.insert(QStringLiteral("version"), 2);
        root.insert(QStringLiteral("profileId"), profileId);
        root.insert(QStringLiteral("regions"), regions);
        if (!titleBar.isEmpty())
            root.insert(QStringLiteral("titleBar"), titleBar);
        if (!menus.isEmpty())
            root.insert(QStringLiteral("menus"), menus);
        if (!panels.isEmpty())
            root.insert(QStringLiteral("panels"), panels);
        return QJsonDocument(root).toJson(QJsonDocument::Compact);
    }

    QByteArray minimalProfileJson(const QString& profileId)
    {
        QJsonArray regions;
        regions.append(QJsonObject{ { QStringLiteral("id"), QStringLiteral("center") },
            { QStringLiteral("type"), QStringLiteral("CenterView") } });
        return makeProfileJson(profileId, regions);
    }

    ApiPtr<IShellRuntime> buildRuntime(const QByteArray& profileJson,
        const ShellServices& services)
    {
        ApiPtr<IShellRuntime> runtime(createShellRuntime(services));
        if (runtime)
            runtime->loadFromJson(profileJson);
        return runtime;
    }

    TranslationGuard::TranslationGuard(const QString& language, const QString& directory)
        : m_service(createTranslationService())
    {
        QString targetDirectory = directory;
        if (targetDirectory.trimmed().isEmpty())
        {
#ifdef UI_TEST_TRANSLATIONS_DIR
            targetDirectory = QString::fromUtf8(UI_TEST_TRANSLATIONS_DIR);
#endif
        }
        if (m_service)
            m_service->install(language, targetDirectory);
    }

    TranslationGuard::~TranslationGuard()
    {
        if (m_service)
            m_service->uninstall();
    }

    bool TranslationGuard::isInstalled() const
    {
        return m_service && m_service->isInstalled();
    }

    QString TranslationGuard::lastError() const
    {
        return m_service ? m_service->lastError() : QString();
    }
} // namespace AA::test