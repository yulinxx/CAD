#pragma once

#include <atomic>
#include <functional>
#include <future>
#include <memory>
#include <thread>

#include <QObject>
#include <QPointer>
#include <QString>

#include "ExportContext.h"
#include "ExportOptions.h"
#include "ExportResult.h"
#include "FileIO/IFileParser.h"

class PersistenceService;

class ExportDispatcher;

namespace Eg
{
    class SceneManager;
    class SceneManager3D;
}

/// 导出服务：导出操作的总入口，协调数据收集、格式写入和状态回写
class ExportService : public QObject
{
    Q_OBJECT

public:
    explicit ExportService(QObject* parent = nullptr);
    ~ExportService() override;

    /// 设置导出分发器
    void setDispatcher(ExportDispatcher* dispatcher);

    /// 设置场景管理器（用于收集 2D 图元数据）
    void setSceneManager(Eg::SceneManager* sceneManager);

    /// 设置 3D 场景管理器（用于收集 3D 网格图元）
    void setSceneManager3D(Eg::SceneManager3D* sceneManager3D);

    /// 设置持久化服务（用于导出记录回写）
    void setPersistenceService(PersistenceService* service);

    /// 设置「原生 .apr 加工语义快照」提供器。
    /// 由组合根注入，回调只在**主线程**的导出 Phase 1 调用：读取 LayerManager /
    /// HardwareProfileManager 生成值快照，随 ExportContext 传到后台线程写盘。
    /// 不注入时 hasMachiningSnapshot 保持 false，导出结果会带显式 warning。
    void setNativeMachiningSnapshotProvider(std::function<NativeDocumentSnapshot()> provider);

    /// 设置忙状态回调（替代旧的 UiStateCenter 直接依赖）
    void setBusyStateCallback(std::function<void(bool)> callback);
    /// 设置状态栏提示回调（替代旧的 UiStateCenter 直接依赖）
    void setStatusPromptCallback(std::function<void(const QString&)> callback);

    /// 执行文件导出
    /// @param filePath 目标文件路径
    /// @param options 导出选项
    /// @return 导出结果
    ExportResult exportFile(const QString& filePath, const ExportOptions& options = ExportOptions{});

    /// 执行带完整上下文的导出
    /// @param context 导出上下文
    /// @param options 导出选项
    /// @return 导出结果
    ExportResult exportWithContext(const ExportContext& context, const ExportOptions& options = ExportOptions{});

    /// 异步导出：Phase 1（主线程）收参 + clone 场景图元 + 忙状态；
    /// Phase 2（后台线程）序列化 + 文件写入；
    /// Phase 3（主线程）回写记录 + 发信号 + 清忙状态。
    /// 调用后立即返回，不会阻塞 UI；导出期间禁止再次调用 exportAsync / exportWithContext
    /// （内部用 m_exportBusy 互斥锁保证）。
    /// @param context 导出上下文
    /// @param options 导出选项
    /// @param onComplete 完成回调（主线程执行，可选）
    void exportAsync(const ExportContext& context,
        const ExportOptions& options = ExportOptions{},
        std::function<void(const ExportResult&)> onComplete = nullptr);

    /// 检查指定路径是否可导出
    /// @param reasonOut 可选，返回不可导出的具体原因（服务未初始化 / 格式不支持等），不再静默失败
    bool canExport(const QString& filePath, QString* reasonOut = nullptr) const;

    /// 获取所有支持的导出扩展名列表
    QStringList supportedExtensions() const;

    /// 获取场景中所有图元（用于导出前预览或收集）
    Fio::VecAprEntityPtr collectAllEntities() const;

signals:
    /// 导出开始信号
    void exportStarted(const QString& filePath);
    /// 导出进度信号（0.0 ~ 1.0）
    void exportProgress(float progress);
    /// 导出完成信号
    void exportFinished(const ExportResult& result);

private:
    /// 导出后的状态回写
    /// @return false 表示导出记录（DocumentRepository）写入失败
    bool postExportRecord(const ExportResult& result, const ExportContext& context);

    /// 收掉上一条异步导出 worker：先置取消标志，等完成信号（正常立即返回）后 join；
    /// 必须在主线程调用（std::thread 只能在 owner 线程 join）
    void shutdownAsyncThread();

    /// 主线程采集加工语义快照并写进上下文。异步路径也必须在启动 worker 之前调用。
    void captureMachiningSnapshot(ExportContext& context) const;

    // ===== 成员 =====

    /// 导出分发器（非拥有指针）
    ExportDispatcher* m_dispatcher{ nullptr };
    /// 2D 场景管理器（非拥有指针）
    Eg::SceneManager* m_sceneManager{ nullptr };
    /// 3D 场景管理器（非拥有指针）
    Eg::SceneManager3D* m_sceneManager3D{ nullptr };
    /// 持久化服务（非拥有指针，用于导出记录回写）
    class PersistenceService* m_persistenceService{ nullptr };
    /// 原生 .apr 加工语义快照提供器（主线程调用，结果按值传给后台线程）
    std::function<NativeDocumentSnapshot()> m_machiningSnapshotProvider;
    /// 忙状态回调（替代 UiStateCenter 直接调用）
    std::function<void(bool)> m_busyStateCallback;
    /// 状态栏提示回调（替代 UiStateCenter 直接调用）
    std::function<void(const QString&)> m_statusPromptCallback;

    // ===== 异步导出后台线程管理 =====
    /// 异步导出的后台 worker 线程（exportAsync 启动、析构/重启时收掉）
    std::thread m_asyncThread;
    /// 外部请求取消标志（析构或新 exportAsync 启动前置 true）
    std::atomic<bool> m_asyncCancelFlag{ false };
    /// 当前 worker 的完成信号：worker 在所有返回路径上置位，shutdownAsyncThread
    /// 先等它就绪再 join，替代旧实现里固定的 3 秒 sleep
    std::shared_future<void> m_asyncDone;
    /// 互斥锁：同一时刻只允许一个导出在跑（同步版和异步版共享）
    std::atomic<bool> m_exportBusy{ false };
};
