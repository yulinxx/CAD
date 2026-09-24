/**
 * @file main.cpp
 * @brief 百万级图元性能基准 —— 建立可重复运行的基线，供优化前后对比。
 *
 * 用法：
 *   SanYiPerfBenchmark [entityCount] [csvPath] [runLabel]
 *     entityCount  图元总数，默认 200000；百万级复测请显式传 1000000
 *                  （Debug 构建下 100 万会慢到不可用，默认值刻意取小）
 *     csvPath      可选；给出时把结果追加写入该 CSV，便于多轮对比
 *     runLabel     可选；本轮标记，写入 CSV 的 "# run=" 行，用来说明这一轮
 *                  对应哪次代码改动（如 "after-quadratic-split"）。缺省写 unlabeled
 *
 * 指标覆盖「用户能感知的操作」与「报告里待优化的路径」：
 *   场景构建 / 全选 / 框选 / 点拾取 / 快照捕获 / 批量移动（含撤销重做）/
 *   逐条删除 / 空间索引批量更新 / 状态交换路径的六相位拆分
 *
 * 状态交换拆分用于回答「批量移动的全链路耗时到底花在哪」：快照捕获、矩阵变换、
 * 索引批量更新三段是单独可测的，剩余部分此前是黑盒，故按相位拆成 6 项指标。
 *
 * 说明：
 *   - 测量在单个进程内完成，读多写少的项重复多次取中位数以抑制抖动；
 *     会改变场景状态的项每次自带回滚，保证重复测量互不影响。
 *   - 本基准只测 CPU 侧耗时，不含 GPU 提交与呈现（帧率需真实视口，另行测）。
 */

#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/Edit/SceneEditService.h"
#include "Engine2D/Edit/SceneUndoCommands.h"
#include "Engine2D/Edit/UndoRedoManager.h"
#include "Engine2D/SyEntity/SyArc.h"
#include "Engine2D/SyEntity/SyCircle.h"
#include "Engine2D/SyEntity/SyLine.h"
#include "Engine2D/SyEntity/SyPolygon.h"

#include <Ut/BBox2d.h>
#include <Ut/Mat.h>
#include <Ut/Vec.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace
{
    using Clock = std::chrono::steady_clock;

    /// 防止被测表达式被优化掉
    volatile size_t g_sink = 0;

    struct Result
    {
        std::string name;
        std::string unit;
        double bestMs{ 0.0 };
        double medianMs{ 0.0 };
        std::string note;
    };

    std::vector<Result> g_results;

    double elapsedMs(const Clock::time_point& from, const Clock::time_point& to)
    {
        return std::chrono::duration<double, std::milli>(to - from).count();
    }

    /**
     * @brief 运行指标：fn 内部须自带「测量前准备 + 测量后回滚」，以便重复。
     * @param repeats 重复次数；读多写少的项给 5，会改状态的项给 1~3
     */
    template <typename Fn>
    void measure(const char* name, const char* unit, int repeats, Fn&& fn, const char* note = "")
    {
        std::vector<double> samples;
        samples.reserve(static_cast<size_t>(repeats));

        for (int i = 0; i < repeats; ++i)
        {
            const Clock::time_point begin = Clock::now();
            fn();
            const Clock::time_point end = Clock::now();
            samples.push_back(elapsedMs(begin, end));
        }

        std::sort(samples.begin(), samples.end());
        Result r;
        r.name = name;
        r.unit = unit;
        r.bestMs = samples.front();
        r.medianMs = samples[samples.size() / 2];
        r.note = note;
        g_results.push_back(r);

        std::printf("  %-28s best=%10.2f ms  median=%10.2f ms  %s\n",
            r.name.c_str(),
            r.bestMs,
            r.medianMs,
            r.note.c_str());
        std::fflush(stdout);
    }

    /// 构造一批混合类型图元（线 60% / 圆 20% / 弧 10% / 多边形 10%）
    std::vector<std::unique_ptr<Eg::SyEntity>> makeEntities(size_t count, double extent)
    {
        std::vector<std::unique_ptr<Eg::SyEntity>> entities;
        entities.reserve(count);

        const double step = extent / 1000.0;
        for (size_t i = 0; i < count; ++i)
        {
            const double x = static_cast<double>(i % 1000) * step;
            const double y = static_cast<double>(i / 1000) * step;
            const size_t bucket = i % 10;

            if (bucket < 6)
            {
                auto e = std::make_unique<Eg::SyLine>();
                e->setPointVector({ Ut::Vec2d(x, y), Ut::Vec2d(x + step * 0.8, y + step * 0.5) });
                entities.push_back(std::move(e));
            }
            else if (bucket < 8)
            {
                auto e = std::make_unique<Eg::SyCircle>();
                e->basePoint = Ut::Vec2d(x, y);
                e->dRadius = step * 0.4;
                entities.push_back(std::move(e));
            }
            else if (bucket < 9)
            {
                auto e = std::make_unique<Eg::SyArc>();
                e->basePoint = Ut::Vec2d(x, y);
                e->dRadius = step * 0.4;
                e->dStartAngle = 0.0;
                e->dEndAngle = 1.5707963267948966;
                entities.push_back(std::move(e));
            }
            else
            {
                auto e = std::make_unique<Eg::SyPolygon>();
                std::vector<Ut::Vec2d> verts;
                verts.reserve(12);
                for (int k = 0; k < 12; ++k)
                {
                    const double a = static_cast<double>(k) * 0.5235987755982988;
                    verts.push_back(Ut::Vec2d(x + step * 0.4 * std::cos(a), y + step * 0.4 * std::sin(a)));
                }
                e->setVertices(std::move(verts));
                entities.push_back(std::move(e));
            }
        }
        return entities;
    }
}  // namespace

int main(int argc, char** argv)
{
    size_t entityCount = 200000;
    std::string csvPath;
    std::string runLabel;
    if (argc > 1)
    {
        entityCount = static_cast<size_t>(std::strtoull(argv[1], nullptr, 10));
    }
    if (argc > 2)
    {
        csvPath = argv[2];
    }
    if (argc > 3)
    {
        // 轮次标签：CSV 是多轮追加的，没有标签就分不清哪一块对应哪次改动，
        // 跨轮比较时极易把负载波动当成优化收益（本项目已经踩过这个坑）。
        runLabel = argv[3];
    }
    if (entityCount == 0)
    {
        entityCount = 200000;
    }

    constexpr double kExtent = 100000.0;   // 世界范围，便于框选按比例取子区域
    const size_t kTransformCount = entityCount / 10;
    const size_t kDeleteCount = std::min<size_t>(1000, entityCount / 100);

    std::printf("===============================================================\n");
    std::printf("[SanYi Perf Benchmark]\n");
    std::printf("  entities      : %zu\n", entityCount);
    std::printf("  transform set : %zu\n", kTransformCount);
    std::printf("  build type    : %s\n",
#ifdef NDEBUG
        "Release"
#else
        "Debug"
#endif
    );
    std::printf("===============================================================\n\n");

    Eg::SceneManager scene;
    UndoRedoManager undoMgr(&scene);
    SceneEditService edit(&scene, &undoMgr);

    // ---------- 1. 场景构建 ----------
    std::printf("[scene build]\n");
    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    double allocMs = 0.0;
    {
        const Clock::time_point begin = Clock::now();
        entities = makeEntities(entityCount, kExtent);
        allocMs = elapsedMs(begin, Clock::now());
    }
    std::printf("  %-28s %10.2f ms  %s\n", "entity_alloc", allocMs, "构造对象");

    double addMs = 0.0;
    {
        const Clock::time_point begin = Clock::now();
        scene.addEntities(std::move(entities));
        addMs = elapsedMs(begin, Clock::now());
    }
    std::printf("  %-28s %10.2f ms  %s\n", "scene_add", addMs, "入场景 + 建索引");
    std::printf("  %-28s %10zu\n", "scene_count", scene.getEntityCount());
    g_results.push_back({ "entity_alloc", "ms", allocMs, allocMs, "构造对象" });
    g_results.push_back({ "scene_add", "ms", addMs, addMs, "入场景 + 建索引" });
    std::fflush(stdout);

    // A/B 对照：同样规模下，两条入口的耗时差 —— 用于区分「入场景本身慢」与「某条入口慢」
    {
        const size_t abCount = std::min<size_t>(entityCount, 50000);
        std::printf("\n[insert path A/B] %zu entities\n", abCount);
        {
            Eg::SceneManager probe;
            auto batch = makeEntities(abCount, kExtent);
            const Clock::time_point begin = Clock::now();
            probe.addEntities(std::move(batch));
            const double ms = elapsedMs(begin, Clock::now());
            std::printf("  %-28s %10.2f ms  %s\n", "addEntities", ms, "普通添加");
            g_results.push_back({ "insert_addEntities", "ms", ms, ms, "普通添加" });
        }
        {
            Eg::SceneManager probe;
            auto batch = makeEntities(abCount, kExtent);
            const Clock::time_point begin = Clock::now();
            probe.insertEntitiesPreserveId(std::move(batch));
            const double ms = elapsedMs(begin, Clock::now());
            std::printf("  %-28s %10.2f ms  %s\n", "insertEntitiesPreserveId", ms, "批量保留 id（撤销回插路径）");
            g_results.push_back({ "insert_preserveId", "ms", ms, ms, "批量保留 id" });
        }
        std::fflush(stdout);
    }

    // 全部 id 与指针，供后续指标复用
    Eg::VecSyEntityPtr allEntities = scene.getAllEntities();
    std::vector<Eg::EntityId> allIds;
    allIds.reserve(allEntities.size());
    for (Eg::SyEntity* e : allEntities)
    {
        if (e)
        {
            allIds.push_back(e->id);
        }
    }

    // ---------- 2. 全选 ----------
    std::printf("\n[selection]\n");
    measure("select_all", "ms", 5, [&]() { scene.selectAll(); });
    measure("selected_readback", "ms", 5, [&]() { g_sink += scene.getSelectedEntities().size(); });
    scene.clearSelection();

    // ---------- 3. 框选（覆盖约 10% 面积）----------
    std::printf("\n[query]\n");
    const Ut::BBox2d box10(Ut::Vec2d(0.0, 0.0), Ut::Vec2d(kExtent * 0.32, kExtent * 0.32));
    size_t boxHits = 0;
    measure("query_box_10pct", "ms", 5, [&]() { boxHits = scene.queryByBox(box10, false).size(); });
    std::printf("  %-28s hits=%zu\n", "query_box_10pct ->", boxHits);

    const Ut::BBox2d box1(Ut::Vec2d(0.0, 0.0), Ut::Vec2d(kExtent * 0.1, kExtent * 0.1));
    size_t boxHits1 = 0;
    measure("query_box_1pct", "ms", 5, [&]() { boxHits1 = scene.queryByBox(box1, false).size(); });
    std::printf("  %-28s hits=%zu\n", "query_box_1pct ->", boxHits1);

    // 点拾取：取一批实际存在的点，避免全部落空导致路径不等价
    std::vector<Ut::Vec2d> probePoints;
    probePoints.reserve(1000);
    {
        const size_t stride = std::max<size_t>(1, allEntities.size() / 1000);
        for (size_t i = 0; i < allEntities.size() && probePoints.size() < 1000; i += stride)
        {
            if (allEntities[i])
            {
                probePoints.push_back(allEntities[i]->getBbox().minPt);
            }
        }
    }
    size_t pointHits = 0;
    measure("query_point_x1000", "ms", 3, [&]() {
        size_t hits = 0;
        for (const Ut::Vec2d& p : probePoints)
        {
            hits += scene.queryByPoint(p, 1.0).size();
        }
        pointHits = hits;
    }, "1000 次点拾取");
    std::printf("  %-28s hits=%zu\n", "query_point ->", pointHits);

    // ---------- 4. 快照捕获 ----------
    std::printf("\n[undo data (P0 相关路径)]\n");
    const size_t snapshotCount = std::min(kTransformCount, allIds.size());
    std::vector<Eg::EntityId> transformIds(allIds.begin(), allIds.begin() + static_cast<ptrdiff_t>(snapshotCount));
    measure("snapshot_capture_10pct", "ms", 3, [&]() {
        std::vector<Eg::SyEntity*> snaps(transformIds.size(), nullptr);
        const size_t written =
            Eg::captureEntitySnapshots(&scene, transformIds.data(), transformIds.size(), snaps.data(), snaps.size());
        for (Eg::SyEntity* s : snaps)
        {
            delete s;
        }
        (void)written;
    }, "captureEntitySnapshots（含克隆）");

    // ---------- 5. 批量移动 + 撤销/重做 ----------
    const Ut::Mat3d moveMat = Ut::Mat3d::translate(1.0, 1.0);
    auto applyMove = [&]() {
        for (Eg::EntityId id : transformIds)
        {
            if (Eg::SyEntity* e = scene.findSyEntityById(id))
            {
                e->transform(moveMat);
            }
        }
    };

    // 纯变换：只跑 mutator 循环（不建快照、不入撤销栈、不动索引），作为对照基准
    measure("transform_apply_only", "ms", 3, [&]() {
        for (Eg::EntityId id : transformIds)
        {
            if (Eg::SyEntity* e = scene.findSyEntityById(id))
            {
                e->transform(moveMat);
                e->transform(Ut::Mat3d::translate(-1.0, -1.0));   // 原地抵消，保持状态不变
            }
        }
    }, "仅矩阵变换（无快照/索引/撤销）");

    // 批量移动：整条链路（快照捕获 + 变换 + 通知 + 批量索引 + 入栈）
    measure("transform_10pct", "ms", 3, [&]() {
        edit.transformEntities(transformIds, applyMove, "BenchMove", false);
        undoMgr.undo();
    }, "全链路，测后回滚");

    // 撤销/重做：命令与场景的状态交换（零克隆路径）
    edit.transformEntities(transformIds, applyMove, "BenchMove", false);
    measure("undo_redo_10pct", "ms", 3, [&]() {
        undoMgr.undo();
        undoMgr.redo();
    }, "一次 undo + 一次 redo");
    // 收尾回滚：让场景回到移动前，后面的指标口径一致，也避免继续持有已被换出的对象
    undoMgr.undo();

    // 用户实际感知的一次「批量移动」：不含撤销。
    // transform_10pct 把「撤销」也算在内，无法回答「拖动一次要多久」；
    // 这里只计 transformEntities 本身，用于复原的 undo 放在计时之外。
    std::printf("\n[user-facing apply]\n");
    {
        const int kApplyRounds = 3;
        std::vector<double> samples;
        samples.reserve(kApplyRounds);
        double restoreMs = 0.0;
        for (int round = 0; round < kApplyRounds; ++round)
        {
            const Clock::time_point t = Clock::now();
            edit.transformEntities(transformIds, applyMove, "BenchApply", false);
            samples.push_back(elapsedMs(t, Clock::now()));

            const Clock::time_point tr = Clock::now();
            undoMgr.undo();   // 仅用于复原，不计入 apply
            restoreMs += elapsedMs(tr, Clock::now());
        }
        std::sort(samples.begin(), samples.end());
        Result r;
        r.name = "transform_apply_10pct";
        r.unit = "ms";
        r.bestMs = samples.front();
        r.medianMs = samples[samples.size() / 2];
        r.note = "一次批量移动（不含撤销）";
        g_results.push_back(r);
        std::printf("  %-28s best=%10.2f ms  median=%10.2f ms  %s\n",
            r.name.c_str(), r.bestMs, r.medianMs, r.note.c_str());
        std::printf("  %-28s %10.2f ms  %s\n", "transform_restore_10pct",
            restoreMs / static_cast<double>(kApplyRounds), "复原用撤销（不计入 apply，仅参考）");
        std::fflush(stdout);
    }

    // 变换目标实体指针。★注意：撤销/重做的状态交换会替换场景中的实体对象，
    // 之前取到的裸指针随即失效（指向已被命令接管的旧对象），因此每次要用时
    // 都必须按 id 重新收集，不能跨一次 swap 复用。
    auto collectTargets = [&]() {
        std::vector<Eg::SyEntity*> targets;
        targets.reserve(snapshotCount);
        for (Eg::EntityId id : transformIds)
        {
            if (Eg::SyEntity* e = scene.findSyEntityById(id))
            {
                targets.push_back(e);
            }
        }
        return targets;
    };

    // ---------- 5.5 状态交换路径拆分 ----------
    // 动机：transform_10pct 全链路约 1129 ms，而快照捕获(49) + 矩阵变换(26) + 索引批量(68)
    // 三段之和只有约 143 ms，剩余约 87% 落在「命令入栈 + 状态交换」上，此前没有任何
    // 单独指标，无法判断下一步该优化哪一段。这里按与 transformEntities 等价的分步链路
    // 逐相位计时，把这段开销拆开。
    std::printf("\n[swap path breakdown]\n");
    {
        const int kSwapRounds = 3;
        std::vector<double> captureSamples, applySamples, bulkSamples, pushSamples, undoSamples, redoSamples;
        captureSamples.reserve(kSwapRounds);
        applySamples.reserve(kSwapRounds);
        bulkSamples.reserve(kSwapRounds);
        pushSamples.reserve(kSwapRounds);
        undoSamples.reserve(kSwapRounds);
        redoSamples.reserve(kSwapRounds);

        double totalMs = 0.0;
        for (int round = 0; round < kSwapRounds; ++round)
        {
            const Clock::time_point roundBegin = Clock::now();

            Clock::time_point t = Clock::now();
            auto before = edit.captureSnapshots(transformIds);
            captureSamples.push_back(elapsedMs(t, Clock::now()));

            t = Clock::now();
            applyMove();
            applySamples.push_back(elapsedMs(t, Clock::now()));

            // 上一轮以 undo 结束，场景对象已换过一轮，指针必须重新收集；
            // 收集本身不计入相位耗时。
            std::vector<Eg::SyEntity*> roundTargets = collectTargets();

            t = Clock::now();
            scene.updateEntityBoundsBulk(roundTargets);
            bulkSamples.push_back(elapsedMs(t, Clock::now()));

            t = Clock::now();
            edit.pushExecutedChange(std::move(before), "BenchSwap", false, Eg::SnapshotScope::Subset);
            pushSamples.push_back(elapsedMs(t, Clock::now()));

            t = Clock::now();
            undoMgr.undo();
            undoSamples.push_back(elapsedMs(t, Clock::now()));

            t = Clock::now();
            undoMgr.redo();
            redoSamples.push_back(elapsedMs(t, Clock::now()));

            totalMs += elapsedMs(roundBegin, Clock::now());

            // 回到变换前，下一轮口径一致
            undoMgr.undo();
        }

        auto reportPhase = [&](const char* name, std::vector<double>& samples, const char* note) {
            std::sort(samples.begin(), samples.end());
            Result r;
            r.name = name;
            r.unit = "ms";
            r.bestMs = samples.front();
            r.medianMs = samples[samples.size() / 2];
            r.note = note;
            g_results.push_back(r);
            std::printf("  %-28s best=%10.2f ms  median=%10.2f ms  %s\n",
                r.name.c_str(), r.bestMs, r.medianMs, r.note.c_str());
        };

        reportPhase("swap_phase_capture", captureSamples, "1/6 捕获 before 快照（含克隆）");
        reportPhase("swap_phase_apply", applySamples, "2/6 矩阵变换");
        reportPhase("swap_phase_index_bulk", bulkSamples, "3/6 索引批量更新");
        reportPhase("swap_phase_push", pushSamples, "4/6 命令入栈（构造快照命令）");
        reportPhase("swap_phase_undo", undoSamples, "5/6 撤销 = 状态换出");
        reportPhase("swap_phase_redo", redoSamples, "6/6 重做 = 状态换入");

        const double roundMs = totalMs / static_cast<double>(kSwapRounds);
        std::printf("  %-28s %10.2f ms  %s\n", "swap_round_total", roundMs,
            "上述六相位之和（含一次 undo + 一次 redo）");
        std::fflush(stdout);
    }

    // ---------- 6. 空间索引批量更新 ----------
    // ★旧写法是拿静止几何反复调 updateEntityBoundsBulk，而 updateBulk 内部对
    //   「包围盒没变」的条目直接 continue（EntitySpatialIndex.cpp:275-276），
    //   量出来的 60 ms 基本是 N 次比较的代价，会严重低估真实开销。
    //   这里每轮先把图元真的移动一次再更新，只给更新计时，复原放在计时之外。
    std::printf("\n[spatial index]\n");
    {
        const std::vector<Eg::SyEntity*> boundsTargets = collectTargets();
        const Ut::Mat3d backMat = Ut::Mat3d::translate(-1.0, -1.0);
        const int kBulkRounds = 3;
        std::vector<double> samples;
        samples.reserve(kBulkRounds);
        for (int round = 0; round < kBulkRounds; ++round)
        {
            for (Eg::SyEntity* e : boundsTargets)
            {
                if (e)
                {
                    e->transform(moveMat);
                }
            }

            const Clock::time_point t = Clock::now();
            scene.updateEntityBoundsBulk(boundsTargets);
            samples.push_back(elapsedMs(t, Clock::now()));

            // 复原几何与索引，使下一轮口径一致（不计时）
            for (Eg::SyEntity* e : boundsTargets)
            {
                if (e)
                {
                    e->transform(backMat);
                }
            }
            scene.updateEntityBoundsBulk(boundsTargets);
        }

        std::sort(samples.begin(), samples.end());
        Result r;
        r.name = "update_bounds_bulk_10pct";
        r.unit = "ms";
        r.bestMs = samples.front();
        r.medianMs = samples[samples.size() / 2];
        r.note = "10 万图元确实移动后的索引 remove+insert";
        g_results.push_back(r);
        std::printf("  %-28s best=%10.2f ms  median=%10.2f ms  %s\n",
            r.name.c_str(), r.bestMs, r.medianMs, r.note.c_str());
        std::fflush(stdout);
    }

    // ---------- 7. 逐条删除 ----------
    std::printf("\n[delete]\n");
    measure("delete_single_xN", "ms", 1, [&]() {
        std::vector<Eg::SyEntity*> victims;
        victims.reserve(kDeleteCount);
        for (size_t i = 0; i < kDeleteCount && i < allIds.size(); ++i)
        {
            if (Eg::SyEntity* e = scene.findSyEntityById(allIds[i]))
            {
                victims.push_back(e);
            }
        }
        for (Eg::SyEntity* e : victims)
        {
            scene.deleteEntity(e);
        }
    }, "逐条删除，只测一次（测后场景少 N 个图元）");
    std::printf("  %-28s %10zu\n", "scene_count_after_delete", scene.getEntityCount());

    // ---------- 汇总 ----------
    std::printf("\n===============================================================\n");
    std::printf("[summary] %zu entities\n", entityCount);
    std::printf("===============================================================\n");
    for (const Result& r : g_results)
    {
        std::printf("  %-28s %10.2f ms\n", r.name.c_str(), r.medianMs);
    }

    if (!csvPath.empty())
    {
        const bool needHeader = [&]() {
            std::ifstream probe(csvPath);
            return !probe.good() || probe.peek() == std::ifstream::traits_type::eof();
        }();

        std::ofstream out(csvPath, std::ios::app);
        if (out.good())
        {
            if (needHeader)
            {
                out << "entities,metric,unit,median_ms,note\n";
            }
            // 每轮写一行以 '#' 开头的轮次标记：不是数据行，但能让"这一块是哪次跑的"一目了然。
            // 缺了它就只能靠数块的位置来指代，而负载波动会让相邻两块的数字差出两三倍。
            {
                char stamp[32] = { 0 };
                const std::time_t now = std::time(nullptr);
                std::tm local{};
#if defined(_WIN32)
                localtime_s(&local, &now);
#else
                localtime_r(&now, &local);
#endif
                std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &local);
                const std::string label = runLabel.empty() ? std::string("unlabeled") : runLabel;
                out << "# run=" << label << " entities=" << entityCount
                    << " at=" << stamp << '\n';
            }
            for (const Result& r : g_results)
            {
                out << entityCount << ',' << r.name << ',' << r.unit << ',' << r.medianMs << ',' << r.note << '\n';
            }
            out.flush();
            std::printf("\n[CSV] appended to %s\n", csvPath.c_str());
        }
        else
        {
            std::printf("\n[CSV] cannot open %s\n", csvPath.c_str());
        }
    }

    return 0;
}
