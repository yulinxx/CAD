#pragma once

#include <cstdint>
#include <functional>
#include <unordered_set>
#include <vector>

#include "Ut/BBox2d.h"

namespace Eg
{
    class SceneManager;
    class SyEntity;
}

class RenderWidget;

/// @brief 渲染层「跨帧提交」调度器 —— 两条共用消费泵的队列
///
/// 本类同时承担两件**职责独立、但共用同一条提交链路**的工作，因此放在一起：
///
///  ① **曲线精度 LOD 队列**（m_queue）
///     按当前缩放/可视区域判断曲线类图元是否需要重 tessellate，过期时把
///     曲线 ID 收进队列，跨多帧分批提交，避免一次性重算上万曲线造成单帧卡顿。
///
///  ② **视口扩展队列**（m_extendQueue）
///     相机移出已装配范围时，新进视野但从未上 GPU 的图元 ID 进此队列，
///     跨帧补传并回收移出视野的图元，替代原先「可视盒越界 → 整场景全量重建」。
///
/// 为什么不拆成两个类：两条队列都通过 processBatch() 提交到同一个 RenderSceneBuilder，
/// 共享同一个消费泵、同一套批次预算与 GL 上下文切换。拆开需要把泵/账本/批次预算
/// 再抽象一层，收益只是命名更纯，风险却是打断当前已稳定的提交顺序（扩展优先）。
/// 所以保留单类，但在接口层面明确分区。
class RenderSubmitScheduler
{
public:
    // ======================== 调参常量（唯一来源） ========================
    //
    // 过去这些值散落在 cpp 各函数的局部 constexpr 与协调器的 setInterval 里，
    // 调一个参数要找好几个文件。现在全部收敛到此处，测试与文档都引用同一份。

    /// 缩放升级滞后比：放大超过该倍数才重建（防抖死区下界）
    static constexpr double kLodUpgradeRatio = 1.3;
    /// 缩放降级滞后比：缩小超过该倍数才重建（防抖死区上界）
    static constexpr double kLodDowngradeRatio = 2.0;
    /// 收集盒相对可视盒的余量倍数：减少平移/微缩放时反复重新收集的边缘抖动
    static constexpr double kCollectBoxMargin = 1.25;
    /// 单批提交耗时预算（毫秒），自适应批量的目标值
    static constexpr double kBatchBudgetMs = 6.0;
    /// 自适应批量下限
    static constexpr size_t kMinBatchSize = 128;
    /// 自适应批量上限
    static constexpr size_t kMaxBatchSize = 8192;
    /// 消费泵定时器间隔（毫秒）。定时器由协调器持有，间隔统一引用本常量
    static constexpr int kPumpIntervalMs = 32;

    /// 提交失败后的重试退避上限（泵 tick 数）。残余失败（几何仓 allocBlock /
    /// writeBlock / upsertDrawItem 写入失败、检查时可用提交时失败的 TOCTOU）
    /// 按 2^(n-1) 个 tick 指数退避后原样重试，退避封顶 16 个 tick
    /// （kPumpIntervalMs=32ms × 16 ≈ 512ms）：既避免每 32ms 空转重试烧 CPU，
    /// 又保证最迟半秒内自动恢复重试，不会永久卡住。失败条目永不丢弃。
    static constexpr uint32_t kSubmitRetryBackoffMaxTicks = 16;

    /// 缩放滞后判据（纯函数，供测试直测）：
    /// 首次（scaleAtBuild <= 0）必过期；此后放大超过 kLodUpgradeRatio 或
    /// 缩小超过 kLodDowngradeRatio 才过期，两者之间是防抖死区。
    static bool isScaleStale(double worldToScreenScale, double scaleAtBuild)
    {
        if (scaleAtBuild <= 0.0)
        {
            return true;
        }
        if (worldToScreenScale > scaleAtBuild * kLodUpgradeRatio)
        {
            return true;
        }
        return worldToScreenScale * kLodDowngradeRatio < scaleAtBuild;
    }

    explicit RenderSubmitScheduler(RenderWidget* widget);
    ~RenderSubmitScheduler() = default;

    RenderSubmitScheduler(const RenderSubmitScheduler&) = delete;
    RenderSubmitScheduler& operator=(const RenderSubmitScheduler&) = delete;

    // ======================== 依赖注入 ========================

    /// 视口切换时更新目标 widget
    void setRenderWidget(RenderWidget* widget) { m_renderWidget = widget; }

    /// 绑定/解绑场景（与协调器共用同一入口时机）
    void setSceneManager(Eg::SceneManager* sm) { m_sceneManager = sm; }
    Eg::SceneManager* sceneManager() const { return m_sceneManager; }

    /// 注入消费泵：队列还有剩余时由协调器重启 kPumpIntervalMs 单次定时器。
    /// 本类不持有 QTimer；未注入时队列只能靠外部驱动。
    void setPumpCallback(std::function<void()> pump)
    {
        m_pump = std::move(pump);
    }

    /// 注入「选中时隐藏本体」实时判定：返回 true 的图元本体不应被批次重新提交
    /// 到世界层（否则缩放后选中本体又会冒出来）。传谓词而不传选中集副本，
    /// 每次判定都读取协调器最新状态，不会因拖动强制显示、轮廓预算翻转而漏同步。
    void setSelectionHiddenPredicate(std::function<bool(uint64_t)> predicate)
    {
        m_isSelectionHidden = std::move(predicate);
    }

    /// 批次提交函数类型：入参为本批图元 ID，返回是否提交成功（失败不写账本）
    using SubmitBatchFn = std::function<bool(const std::vector<uint64_t>&)>;

    /// 测试缝隙：注入后批次提交走回调而非 RenderWidget::upsertRenderEntities，
    /// 让行为测试可以无 GL 上下文验证队列/批次/账本语义。生产路径不注入。
    void setSubmitBatchCallback(SubmitBatchFn fn)
    {
        m_submitBatch = std::move(fn);
    }

    /// 注入「上传通道可用性」判定：返回 false 时 processBatch 挂起队列
    /// （不建批、不提交、不动游标），只留 kPumpIntervalMs 低频探测，恢复后
    /// 自动续跑。用于覆盖「整批必然失败」的前置情形（GL 上下文不可用、
    /// 渲染场景无效），避免无意义的提交尝试与退避重试。
    /// 未注入时走 RenderWidget::isUploadAvailable()；注入了批次提交回调
    /// （测试）而未注入本谓词时默认可用 —— 测试环境没有 GL。
    void setUploadAvailablePredicate(std::function<bool()> predicate)
    {
        m_uploadAvailable = std::move(predicate);
    }

    // ======================== ① 曲线精度 LOD 队列 ========================

    /// 相机变化时调用：按「缩放滞后 + 可视区域」判断是否重新收集 LOD 队列
    void requestRefresh();

    /// 每帧消费一批（自适应批量，按上一批实测耗时反馈调节），整批只切一次
    /// GL 上下文、只请求一次重绘。renderedIds 为「已提交到渲染系统的图元 ID」
    /// 账本，本轮成功提交的会写回其中。**同时消费视口扩展队列（优先）。**
    void processBatch(std::unordered_set<uint64_t>& renderedIds);

    /// LOD 队列排空后的收尾：清队列 + 处理「排队期间又要求重新收集」的待办
    void finishQueue();

    /// 全量重建后调用：清 LOD 与扩展队列，并把缩放基准锚定到当前值
    void clearAfterFullRefresh(double worldToScreenScale);

    /// 仅清空 LOD 队列与游标，**不动缩放基准**（供 updateSceneRender 的全量前置
    /// 兜底；真正的锚定由随后的 applyFullRefresh → clearAfterFullRefresh 完成）
    void clearQueue();

    // ======================== ② 视口扩展队列 ========================

    /// 新进视野但从未上 GPU 的图元 ID 入队（增量补入，替代「可视盒越界 → 全量重建」）。
    /// 优先于 LOD 队列消费 —— 画面缺块比精度更新紧急。ID 重复入队安全
    /// （重复 upsert 按内容哈希短路），但由调用方按账本去重后传入。
    void enqueueViewportExtension(const std::vector<uint64_t>& ids);

    /// 扩展队列是否还有未消费的条目
    bool hasExtensionWork() const { return m_extendCursor < m_extendQueue.size(); }

    /// 仅清空视口扩展队列（LOD 队列保留）：视口范围每次重新对账前调用，
    /// 让扩展队列按当前账本/可视盒重算，不残留上一轮未消费的过期 ID。
    void clearExtensionQueue()
    {
        m_extendQueue.clear();
        m_extendCursor = 0;
    }

    /// 视口增量扩展（对账）完成后调用：清空已作废的 LOD 队列（其收集范围随
    /// 视口移动失效）并记录新的已覆盖盒。**不动缩放基准 m_scaleAtBuild** ——
    /// 留待下一次 requestRefresh 按真实缩放差判断是否需要对已在 GPU 上的
    /// 曲线做精度升/降级（扩展队列只补新进视野的图元，不重刷已在 GPU 上的曲线精度）。
    void markViewportReconciled(const Ut::BBox2d& coveredBox);

    // ======================== 状态查询 ========================

    /// 两队列合计的**剩余未消费**条目数（诊断/回归测试用）：
    /// LOD 队列与扩展队列同样扣除游标已消费的部分，随消费单调递减。
    /// 口径统一后本值只回答「还剩多少没提交」，不代表队列总长。
    size_t size() const
    {
        return (m_queue.size() - m_cursor) + (m_extendQueue.size() - m_extendCursor);
    }

    /// 无剩余未消费条目 —— 与 size() == 0 严格同义（两队列同一口径）。
    /// 扩展队列优先消费，剩余额度才给 LOD。
    bool empty() const { return size() == 0; }

    /// 已执行的批次数（诊断/回归测试用）。单调递增，不随队列清空复位，
    /// 用于观察「单次定时器是否还在续跑」——旧缺陷下该值只会停在 1~2。
    size_t processedBatchCount() const { return m_processedBatches; }

private:
    void refillQueueIfStale();
    void ensurePump();

    /// 上传通道当前是否可用（建批前查询）。不可用时 processBatch 挂起队列，
    /// 不做任何提交尝试 —— 覆盖「整批必然失败」的前置情形。
    bool submitAvailable() const;

    /// 该图元此刻是否应该出现在世界层：自身可见、所在图层可见，
    /// 且没有被「选中时隐藏本体」过滤掉。
    bool isRenderable(uint64_t uid, const Eg::SyEntity* entity) const;

    RenderWidget* m_renderWidget;
    Eg::SceneManager* m_sceneManager{ nullptr };

    /// 「选中时隐藏本体」的实时判定，由 SceneRefreshCoordinator 注入
    std::function<bool(uint64_t)> m_isSelectionHidden;
    /// 消费泵回调，由 SceneRefreshCoordinator 注入（重启 kPumpIntervalMs 单次定时器）
    std::function<void()> m_pump;
    /// 测试缝隙：批次提交回调，未注入时走 RenderWidget::upsertRenderEntities
    SubmitBatchFn m_submitBatch;
    /// 「上传通道可用性」判定（测试缝隙），为空时走 RenderWidget::isUploadAvailable()
    std::function<bool()> m_uploadAvailable;

    // ---- ① 曲线精度 LOD 队列 ----
    std::vector<uint64_t> m_queue;
    size_t m_cursor = 0;
    size_t m_processedBatches = 0;
    /// 上次收集队列时的 worldToScreenScale（缩放滞后判据的基准）。<= 0 表示还没收集过。
    double m_scaleAtBuild{ 0.0 };
    /// 上次收集队列时覆盖的世界矩形（含余量）。可视区域一旦越出它就必须重新收集，
    /// 否则平移后新进视野的图元会一直停在旧精度。
    Ut::BBox2d m_collectedBox;
    bool m_collectedBoxValid{ false };
    /// 队列尚在消费期间又出现了新的收集请求：排空后立即按当时的状态重新评估
    bool m_recollectPending{ false };

    // ---- ② 视口扩展队列 ----
    /// 新进视野但从未上 GPU 的图元 ID（替代全量重建的增量补入）。
    /// 与 LOD 队列相互独立：LOD 管「已有几何的精度升级/降级」，
    /// 扩展管「还没上过 GPU 的图元补传」，消费时扩展优先。
    std::vector<uint64_t> m_extendQueue;
    size_t m_extendCursor = 0;

    // ---- 两条队列共用的提交参数 ----
    /// 自适应批次大小：按上一批 upsertRenderEntities 的实测耗时反馈调节，
    /// 把单批耗时压在 kBatchBudgetMs 附近。默认 2000 与旧固定值一致，
    /// 首批实测后立即收敛。
    size_t m_adaptiveBatchSize{ 2000 };

    // ---- 提交失败的重试状态（两队列共用） ----
    /// 连续提交失败次数（成功即清零），决定退避时长
    uint32_t m_submitFailures{ 0 };
    /// 当前退避剩余 tick 数：> 0 时 processBatch 只递减不提交
    uint32_t m_retryBackoffTicks{ 0 };
    /// 出现过提交失败且失败条目尚未成功补交。视口对账丢弃未消费队列条目时，
    /// 靠本标记强制一次重收集兜底（见 markViewportReconciled）。
    bool m_submitRetryPending{ false };
};
