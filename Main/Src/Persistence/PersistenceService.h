#pragma once

#include <memory>
#include <string>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <unordered_map>
#include <typeindex>
#include <functional>

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
class DialogStateRepository;
class SqliteRepositoryBase;

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
 *
 * 线程安全：
 *  - 使用活跃操作计数器跟踪正在进行的数据库操作
 *  - shutdown() 等待所有活跃操作完成后才关闭数据库
 *
 * 扩展性：
 *  - 支持通过 registerRepository<T>() 注册自定义仓储类型
 *  - 通过 getRepository<T>() 按类型获取仓储实例
 *  - 新增仓储无需修改 PersistenceService 的成员变量和访问器
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

    /// 安全关闭数据库连接（等待所有活跃操作完成）
    void shutdown();

    /// 数据库连接是否已打开
    bool isOpen() const;

    /// 获取活跃操作计数（用于调试和测试）
    int activeOperationCount() const;

    /// 获取数据库引用（供仓储注册使用）
    Eg::Database& database() { return *m_database; }

    // ---- 仓储注册与获取（开闭原则：新增仓储无需修改此类） ----

    /// 注册仓储实例（按类型索引），注册后可通过 getRepository<T>() 获取
    template <typename T>
    void registerRepository(std::unique_ptr<T> repo)
    {
        m_repositories[typeid(T)] = std::move(repo);
    }

    /// 按类型获取仓储指针，未注册则返回 nullptr
    template <typename T>
    T* getRepository()
    {
        auto it = m_repositories.find(typeid(T));
        if (it != m_repositories.end())
        {
            return static_cast<T*>(it->second.get());
        }
        return nullptr;
    }

    // ---- 传统访问器（向后兼容） ----

    RecentFileRepository* recentFiles();
    WorkspaceSnapshotRepository* workspaceSnapshots();
    LayerRepository* layers();
    SettingsRepository* settings();
    DocumentRepository* documents();

    /// 对话框状态仓储访问器（v5 新增）
    DialogStateRepository* dialogStates();

    /// 最近一次操作的错误信息
    const std::string& lastError() const;

private:
    // 数据库与引导
    std::unique_ptr<Eg::Database> m_database;
    std::unique_ptr<DatabaseBootstrapper> m_bootstrapper;

    // 仓储对象（传统方式持有，向后兼容）
    std::unique_ptr<RecentFileRepository> m_recentFiles;
    std::unique_ptr<WorkspaceSnapshotRepository> m_workspaceSnapshots;
    std::unique_ptr<LayerRepository> m_layers;
    std::unique_ptr<SettingsRepository> m_settings;
    std::unique_ptr<DocumentRepository> m_documents;
    std::unique_ptr<DialogStateRepository> m_dialogStates;

    // 仓储注册表（新机制：按类型索引，支持自定义仓储扩展）
    std::unordered_map<std::type_index, std::unique_ptr<SqliteRepositoryBase>> m_repositories;

    // 状态
    std::string m_lastError;

    // 线程安全：活跃操作追踪
    mutable std::mutex m_activeOpMutex;
    std::condition_variable m_activeOpCv;
    std::atomic<int> m_activeOperations{ 0 };
    bool m_shuttingDown{ false };
};