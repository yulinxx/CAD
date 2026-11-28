#pragma once

#include <atomic>
#include <functional>
#include <future>
#include <memory>
#include <thread>

#include <QObject>
#include <QString>

#include "ImportContext.h"
#include "ImportOptions.h"
#include "ImportResult.h"
#include "FileIO/IFileParser.h"

class ImportDispatcher;
class SceneDocument2D;
class PropertiesPanelWidget;
class SceneEditService;
class SceneEditService3D;
class LayerManager;
class SceneRefreshCoordinator;

namespace Eg
{
    class SceneManager;
    class SceneManager3D;
}  // namespace Eg

/// 导入服务：导入操作的总入口，协调格式识别、解析、文档构建和 UI 刷新
/// 导入流程：识别格式 → 解析 → 构建文档 → 刷新显示 → 回写状态
class ImportService : public QObject
{
    Q_OBJECT

public:
    explicit ImportService(QObject* parent = nullptr);
    ~ImportService() override;

public:
    /// 设置导入分发器（注入已注册好所有读取器的分发器实例）
    void setDispatcher(ImportDispatcher* dispatcher);

    /// 设置场景管理器（用于清空场景等操作）
    void setSceneManager(Eg::SceneManager* sceneManager);

    /// 设置 3D 场景管理器（用于导入网格图元到 3D 场景）
    void setSceneManager3D(Eg::SceneManager3D* sceneManager3D);

    /// 获取 3D 场景管理器
    Eg::SceneManager3D* sceneManager3D() const
    {
        return m_sceneManager3D;
    }

    /// 设置 3D 场景编辑服务（用于将导入的 3D 图元通过事务写入，支持 Undo）
    void setSceneEditService3D(SceneEditService3D* sceneEditService3D);

    /// 设置场景编辑服务（用于将导入的图元通过事务写入文档，支持 Undo）
    void setEditService(SceneEditService* editService);

    /// 设置 2D 场景刷新协调器（可选，非拥有）：导入 phase 3-5 期间抑制视口刷新调度，
    /// 避免进度回调泵送事件循环时对半成品场景做全量重建；导入结束由协调器一次性补刷。
    /// 3D 导入工作台不注入，抑制守卫自动退化为空操作。
    void setRefreshCoordinator(SceneRefreshCoordinator* coordinator);

    /// 设置图层管理器（用于导入后还原源文件图层结构，如 DXF 图层）
    void setLayerManager(LayerManager* layerManager);

    /// 设置忙状态回调（替代旧的 UiStateCenter 直接依赖）
    void setBusyStateCallback(std::function<void(bool)> callback);

    /// 获取忙状态回调（供 RAII 守卫使用）
    const std::function<void(bool)>& busyStateCallback() const
    {
        return m_busyStateCallback;
    }

    /// 设置状态栏提示回调（替代旧的 UiStateCenter 直接依赖）
    void setStatusPromptCallback(std::function<void(const QString&)> callback);

    /// 获取状态栏提示回调（供 RAII 守卫使用）
    const std::function<void(const QString&)>& statusPromptCallback() const
    {
        return m_statusPromptCallback;
    }

    /// 设置视口适配回调（导入完成后刷新视口）
    void setViewportFitCallback(std::function<void()> callback);

    /// 设置场景树刷新回调（导入完成后刷新树结构）
    void setTreeRebuildCallback(std::function<void()> callback);

    /// 设置属性面板刷新回调（导入完成后刷新属性）
    void setPropertyRefreshCallback(std::function<void()> callback);

    /// 设置统一显示刷新回调（导入完成后一次性刷新树、属性、视口等，避免多次回调）
    void setDisplayRefreshCallback(std::function<void()> callback);

    /// 设置工作台切换回调（导入完成后切换工作台）
    /// @param callback 参数为目标工作台 ID
    void setWorkbenchSwitchCallback(std::function<void(const QString&)> callback);

    /// 设置最近文件添加回调（导入完成后添加到最近文件列表）
    /// @param callback 参数为 (文件路径, 归属工作台 "2D"/"3D")
    void setRecentFileAddCallback(std::function<void(const QString&, const QString&)> callback);

    /// 设置当前文档路径更新回调（导入完成后更新当前文档路径）
    void setCurrentDocumentPathCallback(std::function<void(const QString&)> callback);

    /// 设置文档持久化回调（导入完成后保存文档记录）
    /// @param callback 参数为 (文件路径, 图元数量, 归属工作台 "2D"/"3D")
    /// 返回 false 表示历史记录写入失败（文件本身已成功导入）
    void setDocumentPersistenceCallback(std::function<bool(const QString&, int, const QString&)> callback);

    /// 执行文件导入
    /// @param filePath 源文件路径
    /// @param options 导入选项
    /// @return 导入结果
    ImportResult importFile(const QString& filePath, const ImportOptions& options = ImportOptions{});

    /// 执行带完整上下文的导入
    /// @param context 导入上下文
    /// @param options 导入选项
    /// @return 导入结果
    ImportResult importWithContext(const ImportContext& context, const ImportOptions& options = ImportOptions{});

    /// 执行异步导入（Phase 1-2 在后台线程，Phase 3-5 回到主线程）
    /// @param context 导入上下文
    /// @param options 导入选项
    /// @param onComplete 导入完成回调（在主线程执行）
    void importAsync(const ImportContext& context,
        const ImportOptions& options = ImportOptions{},
        std::function<void(const ImportResult&)> onComplete = nullptr);

    /// 检查指定路径是否可导入
    /// @param reasonOut 可选，返回不可导入的具体原因（服务未初始化 / 格式不支持等），不再静默失败
    bool canImport(const QString& filePath, QString* reasonOut = nullptr) const;

    /// 获取所有支持的导入扩展名列表
    QStringList supportedExtensions() const;

    /// 获取指定工作台支持的导入扩展名列表
    /// @param workbenchId 工作台 ID ("2D" 或 "3D")
    /// @return 该工作台支持的扩展名列表
    QStringList supportedExtensions(const QString& workbenchId) const;

    /// 位图（经 QImage/图像解码，不走读取器）导入扩展名
    /// 拖拽与菜单必须共用这一份判据，UI 侧不得再维护图片扩展名列表
    static const QStringList& imageExtensions();

    /// 扩展名（小写，不含点号）是否为位图格式
    static bool isImageExtension(const QString& extension);

    /// 指定工作台可导入的全部扩展名：读取器按维度过滤后 + 位图判据
    /// @param workbenchId "2D" 或 "3D"；为空或未知时按全部读取器能力返回
    QStringList importableExtensions(const QString& workbenchId) const;

    /// 指定格式的实际导入扩展名（来自已注册读取器，避免与 FormatRegistry 登记漂移）
    QStringList extensionsForFormat(Fio::FileFormat format) const;

    /// 检查指定路径是否可在指定工作台导入
    /// @param filePath 文件路径
    /// @param workbenchId 工作台 ID ("2D" 或 "3D")
    /// @param reasonOut 可选，返回不可导入的具体原因（格式不支持 / 工作台不兼容）
    /// @return 是否可以导入
    bool canImport(const QString& filePath, const QString& workbenchId, QString* reasonOut = nullptr) const;

signals:
    /// 导入开始信号
    void importStarted(const QString& filePath);
    /// 导入阶段变化信号
    void importPhaseChanged(ImportPhase phase);
    /// 导入进度信号（0.0 ~ 1.0）
    void importProgress(float progress);
    /// 导入完成信号
    void importFinished(const ImportResult& result);

private:
    /// 1：识别文件格式
    ImportResult phaseDetectFormat(ImportContext& context);

    /// 2：解析文件
    ImportResult phaseParse(const ImportContext& context, Fio::VecAprEntityPtr& outEntities);

    /// 3：构建文档（将图元添加到场景）
    ImportResult phaseBuildDocument(const ImportContext& context,
        Fio::VecAprEntityPtr& entities,
        const ImportOptions& options,
        const ImportResult& parseResult);

    /// 4：刷新显示（工作台、视口、树、属性面板）
    void phaseRefreshDisplay(const ImportContext& context, const ImportResult& result, const ImportOptions& options);

    /// 5：回写状态（状态栏、最近文件、文档记录）
    void phaseWriteBackState(const ImportContext& context, ImportResult& result);

    /// 还原图层结构：
    ///   - 打开原生 .apr（replacingDocument）：restoreNativeDocument，图层表按文件原样重建；
    ///   - 其余（外来格式、追加导入 .apr）：restoreLayersByColor，按颜色落层（图层 = 颜色）。
    /// @param idRemap 转换期 ID → 入库后最终 ID 的修正表（入库前 ID 被换过的图元才有条目）
    /// @param importedEntityIds 本次入场图元的最终 ID
    /// @param replacingDocument 本次是否为「打开/替换文档」（importAsNewDocument=true）
    /// @param outWarnings 可选：图层/工艺还原过程中需要如实告知用户的告警
    /// @return 新建/重建的图层数量
    int restoreImportedLayers(const ImportContext& context,
        const ImportResult& parseResult,
        const std::unordered_map<int64_t, int64_t>& idRemap,
        const std::vector<int64_t>& importedEntityIds,
        bool replacingDocument,
        QStringList* outWarnings = nullptr);

    /**
     * @brief 打开原生 .apr：图层文档按文件原样重建
     *
     * 图层表（id / 顺序 / 显示属性 / 加工语义 / Profile 绑定）、硬件 Profile 值集、ProcessData
     * 回灌的图层工艺参数、加工顺序、工艺细节模型一律以文件为准，整体替换当前图层文档，
     * 不与当前图层按名或按色合并。调用前场景已清空、图层文档已重置。
     * @return 重建的图层数
     */
    int restoreNativeDocument(const ImportResult& parseResult,
        const std::unordered_map<int64_t, int64_t>& idRemap,
        const std::vector<int64_t>& importedEntityIds,
        QStringList* outWarnings);

    /**
     * @brief 按颜色落层（图层 = 颜色）：外来格式与追加导入的 .apr
     *
     * 图元显示色 = 显式色（DXF 真彩 / ACI）优先，否则源图层色（ByLayer）。每种颜色一个图层：
     * 有同色的矢量、非填充、未锁定图层就复用（含内置默认层），否则新建一个该颜色的图层
     * （名称取第一个提供该颜色的源图层名）。落层后显式色清掉、改为随层色，显示不变。
     * 同色图层不能接收图元（锁定/填充/位图层）时不另建同色层（同色只能有一个图层），
     * 而是落到 RGB 最近的可用图层并保留自身显示色、告警；超过每次 64 个新颜色层的上限同样处理。
     * 没有颜色信息的图元留在当前层。
     * @return 新建的图层数
     */
    int restoreLayersByColor(const ImportResult& parseResult,
        const std::unordered_map<int64_t, int64_t>& idRemap,
        const std::vector<int64_t>& importedEntityIds,
        QStringList* outWarnings);

    /// 还原源文件群组结构（DXF 块引用 / SVG 的 g 元素 / OBJ 的 o-g-usemtl 等）
    /// 按 IR 群组表在 SceneManager::groupManager() 中新建群组，重建父子层级，并把图元挂到所属群组
    /// @param idRemap 同 restoreImportedLayers
    /// @return 成功创建的群组数量
    int restoreImportedGroups(const ImportResult& parseResult, const std::unordered_map<int64_t, int64_t>& idRemap);

    /**
     * @brief 载入完成后把 EntityIdGenerator 推进到 2D/3D 场景最大 id 之后
     *
     * 导入保留文件里的 id，而生成器是进程级计数器且重启后从 1 开始。不同步就会
     * 出现「打开文件后新画的图元拿到已被占用的 id、被静默拒绝」。幂等，代价是一次
     * O(N) 遍历（相对导入本身的代价可忽略）。
     */
    void syncIdGeneratorWithScenes();

private:
    /// 更新进度：转发给 context.progressCallback（可跨线程安全消费，如进度对话框的 tracker），
    /// 并发出 importProgress 信号。
    void updateProgress(const ImportContext& context, ImportPhase phase, float progress);

    /// 检查是否已取消
    bool isCanceled(const ImportContext& context) const;

private:
    /// 导入分发器（非拥有指针，由组合根管理生命周期）
    ImportDispatcher* m_dispatcher{ nullptr };
    /// 场景管理器（非拥有指针，用于 clearScene）
    Eg::SceneManager* m_sceneManager{ nullptr };
    /// 3D 场景管理器（非拥有指针，用于导入网格图元）
    Eg::SceneManager3D* m_sceneManager3D{ nullptr };
    /// 3D 场景编辑服务（非拥有指针，用于事务化添加 3D 图元）
    SceneEditService3D* m_sceneEditService3D{ nullptr };
    /// 场景编辑服务（非拥有指针，用于事务化添加图元）
    SceneEditService* m_editService{ nullptr };
    /// 2D 场景刷新协调器（非拥有指针，可选）：导入期间抑制视口刷新调度
    SceneRefreshCoordinator* m_refreshCoordinator{ nullptr };
    /// 图层管理器（非拥有指针，用于导入后还原图层结构）
    LayerManager* m_layerManager{ nullptr };
    /// 忙状态回调（替代 UiStateCenter 直接调用）
    std::function<void(bool)> m_busyStateCallback;
    /// 状态栏提示回调（替代 UiStateCenter 直接调用）
    std::function<void(const QString&)> m_statusPromptCallback;

    /// 视口适配回调
    std::function<void()> m_viewportFitCallback;
    /// 场景树刷新回调
    std::function<void()> m_treeRebuildCallback;
    /// 属性面板刷新回调
    std::function<void()> m_propertyRefreshCallback;
    /// 统一显示刷新回调（批量刷新树、属性、视口等）
    std::function<void()> m_displayRefreshCallback;
    /// 工作台切换回调
    std::function<void(const QString&)> m_workbenchSwitchCallback;
    /// 最近文件添加回调
    std::function<void(const QString&, const QString&)> m_recentFileAddCallback;
    /// 当前文档路径更新回调
    std::function<void(const QString&)> m_currentDocumentPathCallback;
    /// 文档持久化回调
    std::function<bool(const QString&, int, const QString&)> m_documentPersistenceCallback;

    // ===== async import 后台线程管理 =====
    /// 异步导入的后台 worker 线程（importAsync 启动、析构/重启时收掉）
    std::thread m_asyncThread;
    /// 外部请求取消标志（析构或新 importAsync 启动前置 true，worker 每段 phase 前检查）
    std::atomic<bool> m_asyncCancelFlag{ false };
    /// 当前 worker 的完成信号：worker 在所有返回路径上置位，shutdownAsyncThread
    /// 先等它就绪再 join，替代旧实现里固定的 3 秒 sleep
    std::shared_future<void> m_asyncDone;
    /// 是否有一次导入尚未走完（importAsync 发起 → finishOnMainThread 收尾；importWithContext 全程）。
    /// 只在主线程读写。用于拒绝重入：
    ///   - 进度对话框被强制关闭后 worker 仍在解析，此时再发起导入若直接 shutdownAsyncThread，
    ///     会在主线程 join 到上一次解析结束，界面整段卡死；
    ///   - 落库阶段会泵送事件循环，第二次导入的落库若在第一次的落库中途被派发，两批写入会交错。
    bool m_importInFlight{ false };
    /// 收掉上一条异步 worker：先置取消标志，等完成信号（正常立即返回）后 join
    /// 必须在主线程调用（std::thread 只能在 owner 线程 join）
    void shutdownAsyncThread();
};
