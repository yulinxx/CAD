/**
 * @file CamBench.cpp
 * @brief CAM 契约性能基准 —— 只测量，不做任何优化（D7）。
 *
 * 测量三段：
 *   1. input_build  ：构造 ToolpathJobInput（等价于桥接层从抽取结果逐字段深拷贝，
 *                     60% 单段直线 / 40% 四段矩形）；
 *   2. input_copy   ：再把构造好的输入整体深拷贝一次（拷贝构造）；
 *   3. cam_compile  ：Cam::compile 全流程（分层、排序、Rapid、长度统计、包围盒）。
 *
 * "深拷贝占比" = (input_build + input_copy) / (input_build + input_copy + cam_compile)，
 * 作为 D2（工艺参数单一来源）后续是否要消除二次拷贝的决策依据。
 *
 * 内存为按结构体 sizeof + vector capacity 的估算值，不含分配器元数据与碎片。
 */

#include "CAM/CamCore.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace
{
    using CamClock = std::chrono::steady_clock;

    /// 防止被测表达式被优化掉
    volatile size_t g_camSink = 0;

    struct CamTiming
    {
        double bestMs = 0.0;
        double medianMs = 0.0;
        double p95Ms = 0.0;
    };

    double camElapsedMs(const CamClock::time_point& from, const CamClock::time_point& to)
    {
        return std::chrono::duration<double, std::milli>(to - from).count();
    }

    CamTiming camSummarize(std::vector<double> samples)
    {
        CamTiming t;
        if (samples.empty())
        {
            return t;
        }
        std::sort(samples.begin(), samples.end());
        t.bestMs = samples.front();
        t.medianMs = samples[samples.size() / 2];
        const size_t p95Index = static_cast<size_t>(
            std::ceil(static_cast<double>(samples.size() - 1) * 0.95));
        t.p95Ms = samples[std::min(p95Index, samples.size() - 1)];
        return t;
    }

    // 以下辅助函数统一加 cam 前缀，避免与 main.cpp 的同名匿名命名空间符号在 unity 构建中冲突

    /// 估算一个输入树占用的字节数（结构体 + 各层 vector 容量）
    size_t camEstimateInputBytes(const Cam::ToolpathJobInput& input)
    {
        size_t bytes = sizeof(Cam::ToolpathJobInput);
        bytes += input.layers.capacity() * sizeof(Cam::ToolpathJobInput::LayerBucket);

        for (const auto& layer : input.layers)
        {
            bytes += layer.entities.capacity() * sizeof(Cam::EntitySnapshot);
            for (const auto& entity : layer.entities)
            {
                bytes += entity.contours.capacity() * sizeof(Cam::Contour);
                bytes += entity.fillRegion.boundaries.capacity() * sizeof(Cam::Contour);
                for (const auto& contour : entity.contours)
                {
                    bytes += contour.segments.capacity() * sizeof(Cam::PathSegment);
                }
            }
        }
        return bytes;
    }

    /// 估算一个输出计划占用的字节数
    size_t camEstimatePlanBytes(const Cam::CamPlan& plan)
    {
        size_t bytes = sizeof(Cam::CamPlan);
        bytes += plan.layers.capacity() * sizeof(Cam::PlanLayerResult);

        for (const auto& layer : plan.layers)
        {
            bytes += layer.contours.capacity() * sizeof(Cam::PlanContour);
            for (const auto& contour : layer.contours)
            {
                bytes += contour.segments.capacity() * sizeof(Cam::PlanPathSegment);
                bytes += contour.piercePoints.capacity() * sizeof(Ut::Vec2d);
            }
        }
        return bytes;
    }

    /// 构造一份 CAM 输入：60% 单段直线，40% 四段矩形（一实体一轮廓）
    Cam::ToolpathJobInput camMakeInput(size_t entityCount)
    {
        Cam::ToolpathJobInput input;
        std::strncpy(input.jobId, "cam_bench", sizeof(input.jobId) - 1);
        input.jobId[sizeof(input.jobId) - 1] = '\0';
        input.failOnUnmachinable = false;

        Cam::ToolpathJobInput::LayerBucket layer;
        layer.layerId = 1;
        layer.params.powerPercent = 50.0;
        layer.params.speedMmPerSec = 200.0;
        layer.params.rapidSpeedMmPerSec = 300.0;
        layer.params.passes = 1;
        layer.params.chordToleranceMm = 0.01;

        layer.entities.reserve(entityCount);
        const double step = 0.5;

        for (size_t i = 0; i < entityCount; ++i)
        {
            const double x = static_cast<double>(i % 1000) * step;
            const double y = static_cast<double>(i / 1000) * step;
            const bool isSquare = (i % 10) >= 6;

            Cam::EntitySnapshot entity;
            entity.entityId = static_cast<uint64_t>(i + 1);
            entity.layerId = 1;
            entity.kind = Cam::EntitySnapshot::GeometryKind::Contour;

            Cam::Contour contour;
            contour.sourceEntityId = static_cast<uint64_t>(i + 1);
            contour.sourceLayerId = 1;
            contour.closed = isSquare;

            if (isSquare)
            {
                contour.segments.reserve(4);
                const Ut::Vec2d p0(x, y);
                const Ut::Vec2d p1(x + step, y);
                const Ut::Vec2d p2(x + step, y + step);
                const Ut::Vec2d p3(x, y + step);
                const Ut::Vec2d corners[4] = { p0, p1, p2, p3 };
                for (int k = 0; k < 4; ++k)
                {
                    Cam::PathSegment seg;
                    seg.type = Cam::PathSegment::Type::Line;
                    seg.ptStart = corners[k];
                    seg.ptEnd = corners[(k + 1) % 4];
                    contour.segments.push_back(seg);
                }
            }
            else
            {
                Cam::PathSegment seg;
                seg.type = Cam::PathSegment::Type::Line;
                seg.ptStart = Ut::Vec2d(x, y);
                seg.ptEnd = Ut::Vec2d(x + step * 0.8, y + step * 0.5);
                contour.segments.push_back(seg);
            }

            entity.contours.push_back(std::move(contour));
            layer.entities.push_back(std::move(entity));
        }

        input.layers.push_back(std::move(layer));
        return input;
    }

    void camRunOneSize(size_t entityCount, int repeats)
    {
        std::printf("\n[CAM contract bench] entities=%zu repeats=%d\n", entityCount, repeats);

        // ---- 阶段 1：构造输入（等价于桥接层深拷贝）----
        std::vector<double> buildSamples;
        buildSamples.reserve(static_cast<size_t>(repeats));
        size_t inputBytes = 0;
        for (int r = 0; r < repeats; ++r)
        {
            const CamClock::time_point begin = CamClock::now();
            Cam::ToolpathJobInput input = camMakeInput(entityCount);
            const CamClock::time_point end = CamClock::now();
            buildSamples.push_back(camElapsedMs(begin, end));
            inputBytes = camEstimateInputBytes(input);
            g_camSink += input.layers.size();
        }

        // 保留一份输入供后续拷贝与编译阶段复用
        Cam::ToolpathJobInput baseline = camMakeInput(entityCount);

        // ---- 阶段 2：整体深拷贝（拷贝构造）----
        std::vector<double> copySamples;
        copySamples.reserve(static_cast<size_t>(repeats));
        for (int r = 0; r < repeats; ++r)
        {
            const CamClock::time_point begin = CamClock::now();
            Cam::ToolpathJobInput copy = baseline;
            const CamClock::time_point end = CamClock::now();
            copySamples.push_back(camElapsedMs(begin, end));
            g_camSink += copy.layers.size();
        }

        // ---- 阶段 3：Cam::compile ----
        std::vector<double> compileSamples;
        compileSamples.reserve(static_cast<size_t>(repeats));
        size_t planBytes = 0;
        size_t contourCount = 0;
        for (int r = 0; r < repeats; ++r)
        {
            Cam::CamPlan plan;
            Cam::CamResult result;
            const CamClock::time_point begin = CamClock::now();
            Cam::compile(baseline, plan, nullptr, &result);
            const CamClock::time_point end = CamClock::now();
            compileSamples.push_back(camElapsedMs(begin, end));
            planBytes = camEstimatePlanBytes(plan);
            contourCount = static_cast<size_t>(result.contourCount);
            g_camSink += contourCount;
            if (!result.ok)
            {
                std::printf("  [warn] compile failed: %s\n", result.errorText);
            }
        }

        const CamTiming build = camSummarize(buildSamples);
        const CamTiming copy = camSummarize(copySamples);
        const CamTiming compile = camSummarize(compileSamples);

        std::printf("  %-22s best=%9.2f  median=%9.2f  p95=%9.2f ms\n",
                    "input_build", build.bestMs, build.medianMs, build.p95Ms);
        std::printf("  %-22s best=%9.2f  median=%9.2f  p95=%9.2f ms\n",
                    "input_copy_ctor", copy.bestMs, copy.medianMs, copy.p95Ms);
        std::printf("  %-22s best=%9.2f  median=%9.2f  p95=%9.2f ms\n",
                    "cam_compile", compile.bestMs, compile.medianMs, compile.p95Ms);
        std::printf("  input_bytes_est      %.2f MB\n",
                    static_cast<double>(inputBytes) / (1024.0 * 1024.0));
        std::printf("  plan_bytes_est       %.2f MB\n",
                    static_cast<double>(planBytes) / (1024.0 * 1024.0));
        std::printf("  contours             %zu\n", contourCount);

        const double buildPlusCopy = build.medianMs + copy.medianMs;
        const double total = buildPlusCopy + compile.medianMs;
        const double share = total > 0.0 ? buildPlusCopy / total : 0.0;
        std::printf("  deep_copy_share      %.1f%% (build+copy)/(build+copy+compile)\n",
                    share * 100.0);
        std::fflush(stdout);
    }
}

/// 由 Tools/PerfBenchmark/main.cpp 在 BUILD_CAM 时调用
void runCamBench()
{
    const int repeats = 5;
    const size_t sizes[] = { 10000, 100000 };

    std::printf("\n===============================================================\n");
    std::printf("[CAM contract bench] build=%s\n",
#ifdef NDEBUG
        "Release"
#else
        "Debug"
#endif
    );

    for (size_t size : sizes)
    {
        camRunOneSize(size, repeats);
    }

    // 1M 需要约 GB 级内存和更长时间，按需开启：设置 SANYI_CAM_BENCH_1M=1
    const char* include1M = std::getenv("SANYI_CAM_BENCH_1M");
    if (include1M && std::strcmp(include1M, "1") == 0)
    {
        camRunOneSize(1000000, 3);
    }
    else
    {
        std::printf("\n  (1M skipped; set SANYI_CAM_BENCH_1M=1 to enable)\n");
    }

    std::printf("===============================================================\n");
    std::fflush(stdout);
}
