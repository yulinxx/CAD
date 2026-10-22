/**
 * @file PersistenceService.cpp
 * @brief 持久化服务实现
 *
 * 管理数据库连接和所有仓储的生命周期。
 */
#include "PersistenceService.h"

#include <typeindex>

#include "DatabaseBootstrapper.h"
#include "Repositories/SqliteRepositoryBase.h"
#include "Repositories/RecentFileRepository.h"
#include "Repositories/WorkspaceSnapshotRepository.h"
#include "Repositories/LayerRepository.h"
#include "Repositories/SettingsRepository.h"
#include "Repositories/DocumentRepository.h"
#include "Repositories/DialogStateRepository.h"

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

    // 创建对话框状态仓储（v5 新增）
    m_dialogStates = std::make_unique<DialogStateRepository>(*m_database);

    SY_DEBUG("[PersistenceService] Initialized successfully");
    return true;
}

void PersistenceService::shutdown()
{
    // 幂等检查：避免重复 shutdown
    if (!m_database || !m_database->isOpen())
    {
        return;
    }

    // SY_DEBUG("[PersistenceService] Shutting down");

    // 这里原本有一段「等待活跃操作归零」的条件变量等待，但活跃操作计数从来没有被
    // 加过（没有任何 RAII 守卫去 increment），等待条件恒为真 —— 是一段看起来能
    // 保护并发、实际什么都没做的假保护。真正的并发保护在 Eg::Database 内部：
    // 每个操作都持互斥量，close() 也持同一把锁，不会出现「操作做一半被关库」。
    // 仓库层的写入（导入、图层同步、设置）都在 UI 线程，没有后台写库。

    // 清理注册表中的仓储
    m_repositories.clear();

    // 按依赖顺序销毁仓储（与创建顺序相反）
    m_dialogStates.reset();
    m_documents.reset();
    m_settings.reset();
    m_layers.reset();
    m_workspaceSnapshots.reset();
    m_recentFiles.reset();
    m_bootstrapper.reset();
    m_database->close();

    SY_DEBUG("[PersistenceService] Shutdown complete");
    // SY_DEBUG("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n\n");
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

DialogStateRepository* PersistenceService::dialogStates()
{
    return m_dialogStates.get();
}

const std::string& PersistenceService::lastError() const
{
    return m_lastError;
}