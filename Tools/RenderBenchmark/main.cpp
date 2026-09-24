/**
 * @file main.cpp
 * @brief 渲染帧路径基准 —— 在 Null 后端上无头测量「每帧 CPU 耗时」，并输出绘制量计数。
 *
 * 用法：
 *   SanYiRenderBenchmark [entityCount] [csvPath] [runLabel]
 *     entityCount  场景图元数，默认 200000；百万级请显式传 1000000
 *     csvPath      可选；给出时把结果追加写入该 CSV
 *     runLabel     可选；本轮标记，写入 CSV 的 "# run=" 行
 *
 * 为什么挂在 Null 后端上：RenderX 的 Null 后端是**完整语义实现**（见
 * rhi/null/nullBackend.cpp 的说明），它声明支持全部可选特性，因此上层走的是
 * 真实代码路径而不是降级分支；同时不依赖 GPU 与窗口系统，可以在 CI/无人值守
 * 环境下跑出可重复的数字。真实帧率还需要视口，见文末说明。
 *
 * 本基准测的是**帧路径的 CPU 侧**：绘制列表 resolve → AABB 剔除 → 按需排序 →
 * 合批 → 命令提交，以及几何仓脏区刷写。这几步正好是《性能审查报告》指出的
 * 渲染端架构约束所在，也是唯一能在这里量化、能拿来对比优化前后的部分。
 * GPU 执行与呈现（swapBuffers/垂直同步）不在覆盖范围内。
 *
 * 覆盖的用例（每个都单独计时，取 p50/p95）：
 *   1. frame_zoom_fit    视野覆盖全场景（缩到最小）—— 远处图元全精度绘制的代价
 *   2. frame_zoom_10pct  视野覆盖 10% 场景
 *   3. frame_zoom_1pct   视野覆盖 1% 场景（剔除生效的对照）
 *   4. frame_pan         每帧平移视野（镜头拖动，不重排，只重剔除）
 *   5. frame_dirty_1k    每帧改 1 千个图元的包围盒（小范围拖动）
 *   6. frame_dirty_10k   每帧改 1 万个图元（大范围拖动）
 *   7. frame_resort_10k  每帧改 1 万条的排序键 —— 直接量化「全量重排」这一项
 *   8. frame_transient_10k 每帧经瞬态环全量重传 1 万条命令（覆盖层/文字类路径）
 *
 * 说明：
 *   - 排序键刻意打乱：若按索引递增，输入本身就是有序的，stable_sort 会退化成
 *     最好情况，量不出真实重排代价。
 *   - 每个用例先跑预热帧再计时，避免把首帧的分配与缓存填充算进来。
 *   - frame_dirty_* 只在偶数帧把包围盒 +1、奇数帧 -1，状态有界不漂移。
 */

#include "render/renderx.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <string>
#include <vector>

using namespace Render::RT;

namespace
{
    using Clock = std::chrono::steady_clock;

    constexpr uint32_t kSurfaceWidth = 1280;
    constexpr uint32_t kSurfaceHeight = 720;
    constexpr uint32_t kVertexStride = 24;   // VertexFormat::P3C3
    constexpr uint32_t kVerticesPerEntity = 2;
    constexpr uint32_t kBlockBytes = kVertexStride * kVerticesPerEntity;
    constexpr int kWarmupFrames = 20;
    constexpr int kMeasuredFrames = 120;

    struct Result
    {
        std::string name;
        std::string unit;
        double medianMs{ 0.0 };
        double p95Ms{ 0.0 };
        double minMs{ 0.0 };
        std::string note;
    };

    std::vector<Result> g_results;

    double elapsedMs(const Clock::time_point& from, const Clock::time_point& to)
    {
        return std::chrono::duration<double, std::milli>(to - from).count();
    }

    /// 列主序单位矩阵
    void makeIdentity(float out[16])
    {
        for (int i = 0; i < 16; ++i)
        {
            out[i] = 0.0f;
        }
        out[0] = 1.0f;
        out[5] = 1.0f;
        out[10] = 1.0f;
        out[15] = 1.0f;
    }

    void makeViewport(float out[4])
    {
        out[0] = 0.0f;
        out[1] = 0.0f;
        out[2] = static_cast<float>(kSurfaceWidth);
        out[3] = static_cast<float>(kSurfaceHeight);
    }

    std::string formatDouble(double v)
    {
        char buffer[64] = { 0 };
        std::snprintf(buffer, sizeof(buffer), "%.4f", v);
        return buffer;
    }

    int percentileIndex(size_t count, double percentile)
    {
        if (count == 0)
        {
            return 0;
        }
        const double raw = percentile * static_cast<double>(count - 1);
        const int idx = static_cast<int>(raw + 0.5);
        return idx < 0 ? 0 : (idx >= static_cast<int>(count) ? static_cast<int>(count) - 1 : idx);
    }

    /**
     * @brief 跑一个用例：预热若干帧后逐帧计时，输出 p50/p95/min
     * @param perFrame 每帧要执行的全部动作（begin → 提交 → end）
     */
    template <typename Fn>
    void runCase(const char* name, const char* note, Fn&& perFrame)
    {
        for (int i = 0; i < kWarmupFrames; ++i)
        {
            perFrame(i);
        }

        std::vector<double> samples;
        samples.reserve(kMeasuredFrames);
        for (int i = 0; i < kMeasuredFrames; ++i)
        {
            const Clock::time_point begin = Clock::now();
            perFrame(i);
            const Clock::time_point end = Clock::now();
            samples.push_back(elapsedMs(begin, end));
        }

        std::sort(samples.begin(), samples.end());

        Result r;
        r.name = name;
        r.unit = "ms";
        r.minMs = samples.front();
        r.medianMs = samples[samples.size() / 2];
        r.p95Ms = samples[static_cast<size_t>(percentileIndex(samples.size(), 0.95))];
        r.note = note;
        g_results.push_back(r);

        std::printf("  %-24s p50=%8.3f ms  p95=%8.3f ms  min=%8.3f ms  帧上限≈%6.1f fps  %s\n",
            name,
            r.medianMs,
            r.p95Ms,
            r.minMs,
            r.medianMs > 0.0 ? 1000.0 / r.medianMs : 0.0,
            note);
        std::fflush(stdout);
    }

    /// 一帧的常规结构：begin → 提交绘制列表 → end
    void submitListFrame(SessionHandle session, DrawListHandle list, const float viewBounds[4])
    {
        rxSessionBeginFrame(session);
        rxSessionSubmitDrawList(session, list, viewBounds);
        rxSessionEndFrame(session);
    }

    void printDrawListStats(const char* label, RuntimeHandle runtime, DrawListHandle list)
    {
        DrawListStats stats{};
        if (rxDrawListGetStats(runtime, list, &stats) != RxResult::Ok)
        {
            return;
        }
        const double mergeRate = stats.visibleCount > 0
            ? 100.0 * (1.0 - static_cast<double>(stats.drawCallCount) / static_cast<double>(stats.visibleCount))
            : 0.0;
        std::printf("  %-24s entries=%u visible=%u draws=%u 合批率=%.1f%% 累计排序次数=%u\n",
            label,
            stats.entryCount,
            stats.visibleCount,
            stats.drawCallCount,
            mergeRate,
            stats.sortCount);
        std::fflush(stdout);
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
        runLabel = argv[3];
    }
    if (entityCount == 0)
    {
        entityCount = 200000;
    }

    const uint32_t dirtySmall = static_cast<uint32_t>(std::min<size_t>(1000, entityCount));
    const uint32_t dirtyLarge = static_cast<uint32_t>(std::min<size_t>(10000, entityCount));

    std::printf("===============================================================\n");
    std::printf("[SanYi Render Benchmark]\n");
    std::printf("  backend       : Null（无 GPU，测帧路径 CPU 侧）\n");
    std::printf("  entities      : %zu\n", entityCount);
    std::printf("  surface       : %ux%u\n", kSurfaceWidth, kSurfaceHeight);
    std::printf("  warmup/measure: %d / %d frames\n", kWarmupFrames, kMeasuredFrames);
    std::printf("  build type    : %s\n",
#ifdef NDEBUG
        "Release"
#else
        "Debug"
#endif
    );
    std::printf("===============================================================\n\n");

    // ---------- 运行时与表面 ----------
    RuntimeDesc runtimeDesc{};
    runtimeDesc.abiVersion = RENDERX_ABI_VERSION;
    runtimeDesc.backend = Backend::Null;
    runtimeDesc.enableValidation = 0;
    runtimeDesc.transientBufferBytes = 128ull * 1024 * 1024;
    runtimeDesc.applicationName = "SanYiRenderBenchmark";

    const RuntimeHandle runtime = rxRuntimeCreate(&runtimeDesc);
    if (!rxValid(runtime))
    {
        std::printf("[fatal] rxRuntimeCreate failed\n");
        return 1;
    }

    SurfaceDesc surfaceDesc{};
    surfaceDesc.windowKind = NativeWindowKind::None;
    surfaceDesc.presentMode = PresentMode::Fifo;
    surfaceDesc.width = kSurfaceWidth;
    surfaceDesc.height = kSurfaceHeight;
    surfaceDesc.enableDepth = 0;

    const SurfaceHandle surface = rxSurfaceCreate(runtime, &surfaceDesc);
    if (!rxValid(surface))
    {
        std::printf("[fatal] rxSurfaceCreate failed\n");
        return 1;
    }

    SessionDesc sessionDesc{};
    sessionDesc.runtime = runtime;
    sessionDesc.surface = surface;
    sessionDesc.clearColor[0] = 0.1f;
    sessionDesc.clearColor[1] = 0.1f;
    sessionDesc.clearColor[2] = 0.1f;
    sessionDesc.clearColor[3] = 1.0f;

    const SessionHandle session = rxSessionCreate(&sessionDesc);
    if (!rxValid(session))
    {
        std::printf("[fatal] rxSessionCreate failed\n");
        return 1;
    }

    float viewMatrix[16] = {};
    makeIdentity(viewMatrix);
    rxSessionSetViewMatrix(session, viewMatrix);

    // ---------- 场景构建：几何仓 + 保留式绘制列表 ----------
    const Clock::time_point buildBegin = Clock::now();

    GeometryStoreDesc storeDesc{};
    storeDesc.initialBytes = static_cast<uint64_t>(entityCount) * kBlockBytes * 2ull + (64ull << 20);
    storeDesc.maxBytes = storeDesc.initialBytes * 2ull;
    // 粒度必须是顶点步长的约数，否则块之间会出现空隙，合批一次都不会发生
    storeDesc.granularity = 8;
    storeDesc.forIndices = 0;

    const GeometryStoreHandle store = rxGeometryStoreCreate(runtime, &storeDesc);
    if (!rxValid(store))
    {
        std::printf("[fatal] rxGeometryStoreCreate failed\n");
        return 1;
    }

    // 场景按 1000 x N 的网格铺开，单格边长 1.0，整体尺度约 1000 x entityCount/1000
    const uint32_t gridWidth = 1000;
    const double gridDepth = static_cast<double>(entityCount + gridWidth - 1) / static_cast<double>(gridWidth);

    std::vector<DrawCommand> commands(entityCount);
    std::vector<float> aabbs(entityCount * 4, 0.0f);
    std::vector<uint64_t> sortKeys(entityCount, 0);
    std::vector<uint64_t> blockIds(entityCount, 0);

    const float vertexScratch[kVerticesPerEntity * (kVertexStride / sizeof(float))] = { 0.0f };

    for (size_t i = 0; i < entityCount; ++i)
    {
        const double x = static_cast<double>(i % gridWidth);
        const double y = static_cast<double>(i / gridWidth);

        GeometryBlock block{};
        if (rxGeometryAlloc(runtime, store, kBlockBytes, &block) != RxResult::Ok)
        {
            std::printf("[fatal] rxGeometryAlloc failed at i=%zu\n", i);
            return 1;
        }
        rxGeometryWrite(runtime, store, block.id, 0, kBlockBytes, vertexScratch);
        blockIds[i] = block.id;

        DrawCommand command{};
        command.vertexBuffer = block.buffer;
        command.vertexOffset = block.offset;
        command.vertexCount = kVerticesPerEntity;
        command.topology = PrimitiveTopology::Lines;
        command.space = RenderSpace::World;
        command.vertexFormat = VertexFormat::P3C3;
        command.indexType = IndexType::None;

        // 排序键刻意打乱（见文件头说明）：按索引递增会让输入天然有序，
        // 排序退化成最好情况，量不出真实重排代价。
        const uint32_t scrambled = static_cast<uint32_t>(i) * 2654435761u;
        const uint64_t sortKey =
            rxMakeSortKey(0, 0, static_cast<uint16_t>(scrambled >> 16), static_cast<uint16_t>(scrambled));
        command.sortKey = sortKey;
        command.userData = i;
        commands[i] = command;
        sortKeys[i] = sortKey;

        aabbs[i * 4 + 0] = static_cast<float>(x);
        aabbs[i * 4 + 1] = static_cast<float>(y);
        aabbs[i * 4 + 2] = static_cast<float>(x + 1.0);
        aabbs[i * 4 + 3] = static_cast<float>(y + 0.5);
    }

    rxGeometryFlush(runtime, store);

    DrawListDesc listDesc{};
    listDesc.initialCapacity = static_cast<uint32_t>(std::min<size_t>(entityCount, 1000000));
    listDesc.enableMerging = 1;
    listDesc.enableCulling = 1;

    const DrawListHandle list = rxDrawListCreate(runtime, &listDesc);
    if (!rxValid(list))
    {
        std::printf("[fatal] rxDrawListCreate failed\n");
        return 1;
    }

    for (size_t i = 0; i < entityCount; ++i)
    {
        if (rxDrawListUpsert(runtime, list, static_cast<uint32_t>(i), &commands[i], &aabbs[i * 4]) != RxResult::Ok)
        {
            std::printf("[fatal] rxDrawListUpsert failed at i=%zu\n", i);
            return 1;
        }
    }

    const double buildMs = elapsedMs(buildBegin, Clock::now());
    std::printf("[setup]\n");
    std::printf("  %-24s %10.2f ms  %s\n", "scene_build", buildMs, "建几何仓 + 填绘制列表（不计入帧耗时）");
    printDrawListStats("drawlist_after_build", runtime, list);
    std::printf("\n");

    // ---------- 视野矩形 ----------
    const double worldW = static_cast<double>(gridWidth);
    const double worldH = std::max(1.0, gridDepth);

    float viewFit[4] = { -1.0f, -1.0f, static_cast<float>(worldW) + 1.0f, static_cast<float>(worldH) + 1.0f };
    auto centeredView = [&](double fraction, float out[4]) {
        const double w = worldW * fraction;
        const double h = worldH * fraction;
        const double cx = worldW * 0.5;
        const double cy = worldH * 0.5;
        out[0] = static_cast<float>(cx - w * 0.5);
        out[1] = static_cast<float>(cy - h * 0.5);
        out[2] = static_cast<float>(cx + w * 0.5);
        out[3] = static_cast<float>(cy + h * 0.5);
    };

    float view10[4] = {};
    float view1[4] = {};
    centeredView(0.10, view10);
    centeredView(0.01, view1);

    // ---------- 1~3. 缩放档位：同一份列表，不同视野 ----------
    std::printf("[zoom levels]（列表不变，只换视野矩形：看剔除是否生效、绘制量如何变化）\n");
    struct ZoomCase
    {
        const char* name;
        const char* note;
        const float* view;
        uint32_t* visibleOut;
        uint32_t* lineOut;
    };

    uint32_t visibleFit = 0;
    uint32_t visible10 = 0;
    uint32_t visible1 = 0;
    uint32_t lineFit = 0;
    uint32_t line10 = 0;
    uint32_t line1 = 0;
    uint32_t drawFit = 0;

    const ZoomCase zoomCases[3] = {
        { "frame_zoom_fit", "视野覆盖全场景（远景全精度绘制）", viewFit, &visibleFit, &lineFit },
        { "frame_zoom_10pct", "视野覆盖 10% 场景", view10, &visible10, &line10 },
        { "frame_zoom_1pct", "视野覆盖 1% 场景", view1, &visible1, &line1 },
    };

    for (const ZoomCase& zc : zoomCases)
    {
        runCase(zc.name, zc.note, [&](int) { submitListFrame(session, list, zc.view); });

        DrawListStats stats{};
        rxDrawListGetStats(runtime, list, &stats);
        FrameStats frameStats{};
        rxSessionGetStats(session, &frameStats);
        *zc.visibleOut = stats.visibleCount;
        // 本基准用线段图元，因此绘制量体现在 lineCount 而不是 triangleCount
        *zc.lineOut = frameStats.lineCount;
        drawFit = stats.drawCallCount;
    }

    std::printf("  %-24s visible=%u lines=%u draws=%u\n",
        "zoom_fit_counters ->", visibleFit, lineFit, drawFit);
    std::printf("  %-24s visible=%u lines=%u\n", "zoom_10pct_counters ->", visible10, line10);
    std::printf("  %-24s visible=%u lines=%u\n", "zoom_1pct_counters ->", visible1, line1);
    printDrawListStats("drawlist_after_zoom", runtime, list);
    std::printf("\n");

    // ---------- 3b. 对照：关闭剔除（直接走线性路径，不经网格） ----------
    // 用途：全场景视野下，候选必然覆盖全部条目，此时网格粗筛会退化成
    // 「先收集全部候选、再判定收益消失、再回退线性」。这个对照把「网格收集 +
    // 回退判定」这一段的净成本单独量出来——两边都是同一份命令、同一个视野，
    // 唯一差别是走不走网格。
    std::printf("[control: culling disabled]\n");
    {
        DrawListDesc noCullDesc{};
        noCullDesc.initialCapacity = static_cast<uint32_t>(std::min<size_t>(entityCount, 1000000));
        noCullDesc.enableMerging = 1;
        noCullDesc.enableCulling = 0;

        const DrawListHandle noCullList = rxDrawListCreate(runtime, &noCullDesc);
        if (rxValid(noCullList))
        {
            for (size_t i = 0; i < entityCount; ++i)
            {
                rxDrawListUpsert(runtime, noCullList, static_cast<uint32_t>(i), &commands[i], &aabbs[i * 4]);
            }

            runCase("frame_fit_nocull", "全场景视野但关闭剔除（纯线性路径，对照）",
                [&](int) { submitListFrame(session, noCullList, viewFit); });

            // 与 frame_zoom_fit 逐项对比：两者视野相同、命令相同，只差是否经网格
            const Result* fitWithCull = nullptr;
            for (const Result& r : g_results)
            {
                if (r.name == "frame_zoom_fit")
                {
                    fitWithCull = &r;
                }
            }
            if (fitWithCull)
            {
                std::printf("  %-24s %8.3f ms  %s\n",
                    "grid_overhead_at_fit",
                    fitWithCull->medianMs - g_results.back().medianMs,
                    "同视野下「网格收集 + 回退判定」的净成本");
                std::fflush(stdout);
            }

            rxDrawListDestroy(runtime, noCullList);
        }
        else
        {
            std::printf("  [warn] 对照用绘制列表创建失败，跳过\n");
        }
    }
    std::printf("\n");

    // ---------- 4. 镜头平移 ----------
    std::printf("[camera / drag]\n");
    runCase("frame_pan", "每帧平移视野（镜头拖动）", [&](int frame) {
        const double offset = std::fmod(static_cast<double>(frame) * 0.5, worldW * 0.5);
        float view[4] = {};
        centeredView(0.20, view);
        view[0] += static_cast<float>(offset);
        view[2] += static_cast<float>(offset);
        view[1] += static_cast<float>(offset);
        view[3] += static_cast<float>(offset);

        float matrix[16] = {};
        makeIdentity(matrix);
        matrix[12] = static_cast<float>(-offset);
        matrix[13] = static_cast<float>(-offset);

        rxSessionBeginFrame(session);
        rxSessionSetViewMatrix(session, matrix);
        rxSessionSubmitDrawList(session, list, view);
        rxSessionEndFrame(session);
    });

    // ---------- 5~6. 拖动：每帧改 K 个图元的包围盒 ----------
    // 只在偶数帧 +1、奇数帧 -1，位置有界不漂移，各帧代价可比。
    auto runDirtyCase = [&](const char* name, uint32_t count, const char* note) {
        runCase(name, note, [&](int frame) {
            const float delta = (frame % 2 == 0) ? 1.0f : -1.0f;
            for (uint32_t i = 0; i < count; ++i)
            {
                float* box = &aabbs[static_cast<size_t>(i) * 4];
                box[0] += delta;
                box[2] += delta;
                rxDrawListUpsert(runtime, list, i, &commands[i], box);
            }
            submitListFrame(session, list, viewFit);
        });
    };

    runDirtyCase("frame_dirty_1k", dirtySmall, "每帧改 1 千个图元的包围盒");
    runDirtyCase("frame_dirty_10k", dirtyLarge, "每帧改 1 万个图元的包围盒");
    printDrawListStats("drawlist_after_dirty", runtime, list);

    // ---------- 7. 排序键变更：直接量化「全量重排」 ----------
    runCase("frame_resort_10k", "每帧改 1 万条的排序键（触发全量重排）", [&](int frame) {
        for (uint32_t i = 0; i < dirtyLarge; ++i)
        {
            const size_t index = i;
            const uint32_t scrambled = static_cast<uint32_t>(index) * 2654435761u;
            const uint32_t rotated = scrambled + static_cast<uint32_t>(frame);
            commands[index].sortKey =
                rxMakeSortKey(0, 0, static_cast<uint16_t>(rotated >> 16), static_cast<uint16_t>(rotated));
            rxDrawListUpsert(runtime, list, i, &commands[index], &aabbs[index * 4]);
        }
        submitListFrame(session, list, viewFit);
    });

    // 把排序键恢复到建表时的取值，避免影响后续用例
    for (size_t i = 0; i < entityCount; ++i)
    {
        commands[i].sortKey = sortKeys[i];
        rxDrawListUpsert(runtime, list, static_cast<uint32_t>(i), &commands[i], &aabbs[i * 4]);
    }
    printDrawListStats("drawlist_after_resort", runtime, list);
    std::printf("\n");

    // ---------- 8. 瞬态环全量重传（覆盖层 / 世界文字路径） ----------
    std::printf("[transient path]\n");
    {
        const uint32_t transientBytes = dirtyLarge * kBlockBytes;
        std::vector<DrawCommand> transientCommands(dirtyLarge);
        float viewport[4] = {};
        makeViewport(viewport);

        bool allocFailed = false;
        runCase("frame_transient_10k", "每帧经瞬态环全量重传 1 万条命令", [&](int frame) {
            rxSessionBeginFrame(session);

            TransientAlloc alloc{};
            if (rxSessionAllocTransient(session, transientBytes, &alloc) != RxResult::Ok)
            {
                allocFailed = true;
                rxSessionEndFrame(session);
                return;
            }
            std::memset(alloc.cpuPtr, 0, transientBytes);

            for (uint32_t i = 0; i < dirtyLarge; ++i)
            {
                DrawCommand command{};
                command.vertexBuffer = alloc.buffer;
                command.vertexOffset = alloc.offset + i * kBlockBytes;
                command.vertexCount = kVerticesPerEntity;
                command.topology = PrimitiveTopology::Lines;
                command.space = RenderSpace::World;
                command.vertexFormat = VertexFormat::P3C3;
                command.indexType = IndexType::None;
                command.sortKey = rxMakeSortKey(1, 0, 0, static_cast<uint16_t>(i));
                transientCommands[i] = command;
            }

            DrawPacket packet{};
            packet.commands = transientCommands.data();
            packet.commandCount = dirtyLarge;
            packet.enableCulling = 0;
            packet.viewMatrix[0] = 1.0f;
            packet.viewMatrix[5] = 1.0f;
            packet.viewMatrix[10] = 1.0f;
            packet.viewMatrix[15] = 1.0f;
            std::memcpy(packet.viewport, viewport, sizeof(viewport));
            packet.frameId = static_cast<uint64_t>(frame);

            rxSessionSubmit(session, &packet);
            rxSessionEndFrame(session);
        });

        if (allocFailed)
        {
            std::printf("  [warn] rxSessionAllocTransient 失败，瞬态用例数据不可信\n");
        }
    }

    FrameStats finalStats{};
    rxSessionGetStats(session, &finalStats);
    std::printf("  %-24s draws=%u triangles=%u lines=%u culled=%u merged=%u geomUpload=%llu B\n",
        "last_frame_stats ->",
        finalStats.drawCallCount,
        finalStats.triangleCount,
        finalStats.lineCount,
        finalStats.culledCommandCount,
        finalStats.mergedDrawCount,
        static_cast<unsigned long long>(finalStats.geometryUploadBytes));

    // ---------- 汇总 ----------
    std::printf("\n===============================================================\n");
    std::printf("[summary] %zu entities\n", entityCount);
    std::printf("===============================================================\n");
    for (const Result& r : g_results)
    {
        std::printf("  %-24s %10.4f ms\n", r.name.c_str(), r.medianMs);
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
                    << " at=" << stamp << " backend=Null\n";
            }
            for (const Result& r : g_results)
            {
                out << entityCount << ',' << r.name << ',' << r.unit << ',' << r.medianMs << ",p95=" <<
                    formatDouble(r.p95Ms) << ' ' << r.note << '\n';
            }
            out.flush();
            std::printf("\n[CSV] appended to %s\n", csvPath.c_str());
        }
        else
        {
            std::printf("\n[CSV] cannot open %s\n", csvPath.c_str());
        }
    }

    // ---------- 清理 ----------
    rxSessionDestroy(session);
    rxDrawListDestroy(runtime, list);
    rxSurfaceDestroy(runtime, surface);
    rxRuntimeDestroy(runtime);

    return 0;
}
