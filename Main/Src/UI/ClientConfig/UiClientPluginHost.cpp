#include "UiClientPluginHost.h"

#include "UiPanelRegistry.h"

#include "Log/SyLogger.h"
#include "UI/Plugin/CommandPluginRegistryApi.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLibrary>
#include <QStringList>

namespace
{
    using CreateCommandPluginFn = ICommandPlugin* (*)();
    using DestroyCommandPluginFn = void (*)(ICommandPlugin*);

    bool isPluginLibrary(const QString& fileName)
    {
#if defined(Q_OS_WIN)
        return fileName.endsWith(QLatin1String(".dll"), Qt::CaseInsensitive);
#elif defined(Q_OS_MACOS)
        return fileName.endsWith(QLatin1String(".dylib")) || fileName.endsWith(QLatin1String(".so"));
#else
        return fileName.endsWith(QLatin1String(".so"));
#endif
    }

    class RegistrySink final : public UiPanelRegisterSink
    {
    public:
        explicit RegistrySink(UiPanelRegistry& registry)
            : m_registry(registry)
        {
        }

        void registerPanel(const QString& id, std::function<QWidget*(QWidget* parent)> factory) override
        {
            if (id.trimmed().isEmpty() || !factory)
            {
                SY_WARNF("[UiClientPlugin] Ignored invalid panel registration: '%s'", qPrintable(id));
                return;
            }
            if (m_registry.isPanelRegistered(id))
            {
                SY_WARNF("[UiClientPlugin] Panel '%s' already registered, plugin entry ignored", qPrintable(id));
                return;
            }
            m_registry.registerPanel(id, std::move(factory));
        }

    private:
        UiPanelRegistry& m_registry;
    };
}  // namespace

UiClientPluginHost& UiClientPluginHost::instance()
{
    static UiClientPluginHost host;
    return host;
}

int UiClientPluginHost::loadForClient(const QString& clientId)
{
    const QString id = clientId.trimmed();
    if (m_loaded && id == m_loadedClientId)
    {
        return static_cast<int>(m_plugins.size());
    }

    m_loaded = true;
    m_loadedClientId = id;

    int loaded = 0;
    const QString envDir = QString::fromLocal8Bit(qgetenv("SANYI_CLIENT_PLUGIN_DIR")).trimmed();
    if (!envDir.isEmpty())
    {
        loaded += loadDirectory(envDir, id);
    }

    if (!id.isEmpty() && QCoreApplication::instance())
    {
        const QString clientDir =
            QDir(QCoreApplication::applicationDirPath() + QStringLiteral("/plugins/clients")).filePath(id);
        loaded += loadDirectory(clientDir, id);
    }

    SY_INFOF("[UiClientPlugin] Loaded %d client plugin(s) for '%s'", loaded, qPrintable(id));
    return loaded;
}

int UiClientPluginHost::loadDirectory(const QString& directory, const QString& clientId)
{
    const QDir dir(directory);
    if (!dir.exists())
    {
        return 0;
    }

    int loaded = 0;
    const QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    for (const QFileInfo& entry : entries)
    {
        if (!isPluginLibrary(entry.fileName()))
        {
            continue;
        }
        if (loadLibrary(entry.absoluteFilePath(), clientId))
        {
            ++loaded;
        }
    }
    return loaded;
}

bool UiClientPluginHost::loadLibrary(const QString& libraryPath, const QString& clientId)
{
    for (const auto& existing : m_plugins)
    {
        if (existing && existing->path == libraryPath)
        {
            return false;
        }
    }

    auto library = std::make_unique<QLibrary>(libraryPath);
    if (!library->load())
    {
        SY_WARNF(
            "[UiClientPlugin] Failed to load '%s': %s", qPrintable(libraryPath), qPrintable(library->errorString()));
        return false;
    }

    bool registeredSomething = false;
    IClientUiPlugin* uiPlugin = nullptr;
    DestroyClientUiPluginFn uiDestroy = nullptr;

    auto createUi = reinterpret_cast<CreateClientUiPluginFn>(library->resolve("createClientUiPlugin"));
    if (createUi)
    {
        IClientUiPlugin* plugin = createUi();
        if (plugin)
        {
            const QString owner = plugin->clientId().trimmed();
            if (owner.isEmpty() || owner == clientId)
            {
                uiPlugin = plugin;
                uiDestroy = reinterpret_cast<DestroyClientUiPluginFn>(library->resolve("destroyClientUiPlugin"));
                registeredSomething = true;
                SY_INFOF(
                    "[UiClientPlugin] Loaded UI plugin '%s' (%s)", qPrintable(plugin->id()), qPrintable(libraryPath));
            }
            else
            {
                SY_INFOF("[UiClientPlugin] Skip '%s': belongs to client '%s', active '%s'",
                    qPrintable(libraryPath),
                    qPrintable(owner),
                    qPrintable(clientId));
                if (auto destroy = reinterpret_cast<DestroyClientUiPluginFn>(library->resolve("destroyClientUiPlugin")))
                {
                    destroy(plugin);
                }
                else
                {
                    delete plugin;
                }
            }
        }
        else
        {
            SY_WARNF("[UiClientPlugin] createClientUiPlugin() returned null: '%s'", qPrintable(libraryPath));
        }
    }

    auto createCommand = reinterpret_cast<CreateCommandPluginFn>(library->resolve("createCommandPlugin"));
    if (createCommand)
    {
        ICommandPlugin* plugin = createCommand();
        if (plugin)
        {
            auto destroy = reinterpret_cast<DestroyCommandPluginFn>(library->resolve("destroyCommandPlugin"));
            commandPluginRegistryRegister(
                plugin, destroy ? destroy : [](ICommandPlugin* p) {
                    delete p;
                });
            registeredSomething = true;
            SY_INFOF(
                "[UiClientPlugin] Loaded command plugin '%s' (%s)", qPrintable(plugin->id()), qPrintable(libraryPath));
        }
    }

    if (!registeredSomething)
    {
        SY_WARNF("[UiClientPlugin] '%s' exports no supported plugin entry", qPrintable(libraryPath));
        library->unload();
        return false;
    }

    auto loaded = std::make_unique<LoadedPlugin>();
    loaded->plugin = uiPlugin;
    loaded->destroy = uiDestroy;
    loaded->path = libraryPath;
    loaded->library = std::move(library);
    m_plugins.push_back(std::move(loaded));
    return true;
}

void UiClientPluginHost::applyPanels(UiPanelRegistry& registry)
{
    RegistrySink sink(registry);
    for (const auto& loaded : m_plugins)
    {
        if (loaded && loaded->plugin)
        {
            loaded->plugin->registerPanels(sink);
        }
    }
}

QStringList UiClientPluginHost::loadedPluginIds() const
{
    QStringList ids;
    for (const auto& loaded : m_plugins)
    {
        if (loaded && loaded->plugin)
        {
            ids << loaded->plugin->id();
        }
    }
    return ids;
}

QStringList UiClientPluginHost::loadedPluginPaths() const
{
    QStringList paths;
    for (const auto& loaded : m_plugins)
    {
        if (loaded)
        {
            paths << loaded->path;
        }
    }
    return paths;
}