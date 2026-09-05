#include "PersistenceService.h"

#include <thread>

#include "DatabaseBootstrapper.h"
#include "Repositories/RecentFileRepository.h"
#include "Repositories/WorkspaceSnapshotRepository.h"
#include "Repositories/LayerRepository.h"
#include "Repositories/SettingsRepository.h"
#include "Repositories/DocumentRepository.h"

#include "Engine/Persistence/Database.h"
#include "Log/SyLogger.h"

PersistenceService::PersistenceService()
    : m_database(std::make_unique<Eg::Database>())
{
}

PersistenceService::~PersistenceService()
{
    shutdown();
}

bool PersistenceService::initialize(const std::string& dbPath)
{
    SY_DEBUGF("[PersistenceService] Initializing database at: %s", dbPath.c_str());

    if (!m_database->open(dbPath))
    {
        m_lastError = "Failed to open database: " + m_database->lastError();
        SY_ERRORF("[PersistenceService] %s", m_lastError.c_str());
        return false;
    }

    // 确保表结构存在
    m_bootstrapper = std::make_unique<DatabaseBootstrapper>(*m_database);
    if (!m_bootstrapper->ensureSchema())
    {
        m_lastError = m_bootstrapper->lastError();
        SY_ERRORF("[PersistenceService] %s", m_lastError.c_str());
        return false;
    }

    // 创建所有仓储
    m_recentFiles = std::make_unique<RecentFileRepository>(*m_database);
    m_workspaceSnapshots = std::make_unique<WorkspaceSnapshotRepository>(*m_database);
    m_layers = std::make_unique<LayerRepository>(*m_database);
    m_settings = std::make_unique<SettingsRepository>(*m_database);
    m_documents = std::make_unique<DocumentRepository>(*m_database);

    SY_DEBUG("[PersistenceService] Initialized successfully");
    return true;
}

void PersistenceService::shutdown()
{
    if (m_database && m_database->isOpen())
    {
        SY_DEBUG("[PersistenceService] Shutting down");
        // 先等待一小段时间，让其他线程有时间完成数据库操作
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        m_recentFiles.reset();
        m_workspaceSnapshots.reset();
        m_layers.reset();
        m_settings.reset();
        m_documents.reset();
        m_bootstrapper.reset();
        m_database->close();
    }
}

bool PersistenceService::isOpen() const
{
    return m_database && m_database->isOpen();
}

RecentFileRepository* PersistenceService::recentFiles()
{
    return m_recentFiles.get();
}

WorkspaceSnapshotRepository* PersistenceService::workspaceSnapshots()
{
    return m_workspaceSnapshots.get();
}

LayerRepository* PersistenceService::layers()
{
    return m_layers.get();
}

SettingsRepository* PersistenceService::settings()
{
    return m_settings.get();
}

DocumentRepository* PersistenceService::documents()
{
    return m_documents.get();
}

const std::string& PersistenceService::lastError() const
{
    return m_lastError;
}