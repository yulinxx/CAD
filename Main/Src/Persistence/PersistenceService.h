#pragma once

#include <memory>
#include <string>

namespace Eg
{
    class Database;
}
class DatabaseBootstrapper;
class RecentFileRepository;
class WorkspaceSnapshotRepository;
class LayerRepository;
class SettingsRepository;
class DocumentRepository;

/**
 * @brief 持久化服务 — 统一管理数据库连接和所有仓储对象
 *
 * 作为 UI 层访问持久化数据的唯一入口，UI 不直接拼 SQL。
 * 在 ApplicationCompositionRoot 中创建，通过 UiServices 注入到 UI 组件。
 *
 * 生命周期：
 *  - AppInitializer → DatabaseBootstrapper::ensureSchema()
 *  - ApplicationCompositionRoot → 创建 PersistenceService → 注入 UiServices
 *  - WorkbenchWindow → 通过 UiServices 获取，调用各仓储方法
 */
class PersistenceService
{
public:
    PersistenceService();
    ~PersistenceService();

    PersistenceService(const PersistenceService&) = delete;
    PersistenceService& operator=(const PersistenceService&) = delete;

    /// 初始化数据库连接并执行 Schema 检查
    /// @param dbPath 数据库文件完整路径
    /// @return 成功返回 true
    bool initialize(const std::string& dbPath);

    /// 安全关闭数据库连接
    void shutdown();

    /// 数据库连接是否已打开
    bool isOpen() const;

    // ---- 仓储访问器 ----

    RecentFileRepository* recentFiles();
    WorkspaceSnapshotRepository* workspaceSnapshots();
    LayerRepository* layers();
    SettingsRepository* settings();
    DocumentRepository* documents();

    /// 最近一次操作的错误信息
    const std::string& lastError() const;

private:
    // 数据库与引导
    std::unique_ptr<Eg::Database>          m_database;
    std::unique_ptr<DatabaseBootstrapper>    m_bootstrapper;

    // 仓储对象
    std::unique_ptr<RecentFileRepository>      m_recentFiles;
    std::unique_ptr<WorkspaceSnapshotRepository> m_workspaceSnapshots;
    std::unique_ptr<LayerRepository>         m_layers;
    std::unique_ptr<SettingsRepository>      m_settings;
    std::unique_ptr<DocumentRepository>      m_documents;

    // 状态
    std::string                             m_lastError;
};