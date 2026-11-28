/**
 * @file ImportFailurePathTests.cpp
 * @brief 导入 → 渲染链路的失败路径回归（2026-11-22 链路审查修复的守护用例）。
 *
 * 覆盖：
 *   1. 替换文档：解析失败 / 图元全被拒时，旧文档与撤销栈原样保留，不触发清空前回调；
 *   2. 替换文档：清空前回调先于 clearScene 调用（撤销栈不会在已清空的场景上被重放）；
 *   3. 追加导入：从不触发清空前回调，结果不带 documentCleared；
 *   4. 落库之后才请求取消：导入按成功收尾（落库不可回滚，不能报 Canceled）；
 *   5. 重入保护：异步导入未收尾时拒绝新导入，收尾后恢复可用；
 *   6. 打开 .apr 替换文档时同名图层的显示属性以文件为准；追加导入保留用户设置；
 *   7. 全量提交失败：保持全量级别并由定时器重试，不把「未构建」记成「已构建」；
 *   8. 视口扩展：同步补入量超过阈值时升级为全量重建，低于阈值仍走增量。
 *
 * Unity 构建会把多个测试源拼进同一个 TU：辅助代码统一放在具名命名空间 ImportFailurePath 里。
 */

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>

#include "Engine2D/AprEntity/AprLine.h"
#include "Engine2D/AprEntity/AprPolygon.h"
#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/Core/SceneWriteGateway.h"
#include "Engine2D/Edit/SceneEditService.h"
#include "Engine2D/Edit/UndoRedoManager.h"
#include "Engine2D/Interaction/HardwareProfileManager.h"
#include "Engine2D/Interaction/LayerManager.h"

#include "FileIO/AprDocument.h"
#include "FileIO/AprSerializer.h"

#include "Composition/ImportExportServiceSetup.h"
#include "Import/IImportReader.h"
#include "Import/ImportDispatcher.h"
#include "Import/ImportService.h"
#include "Import/Readers/NativeImportReader.h"

#include "RenderWidget.h"
#include "UI/Render/ISceneRefreshScheduler.h"
#include "UI/Render/SceneRefreshCoordinator.h"

namespace ImportFailurePath
{
    /// 进程内有且仅有一个 QApplication（与 CompositionRootInjectionTests 同一约定）。
    /// 异步导入的收尾回调投递到 qApp，RenderWidget 也需要它。
    void ensureApplication()
    {
        if (qApp)
        {
            return;
        }
        static int argc = 1;
        static char appName[] = "MainTests";
        static char* argv[] = { appName, nullptr };
        static QApplication app(argc, argv);
    }

    /// 泵事件循环直到条件成立或超时
    bool pumpUntil(const std::function<bool()>& done, int timeoutMs)
    {
        QElapsedTimer elapsed;
        elapsed.start();
        while (!done() && elapsed.elapsed() < timeoutMs)
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return done();
    }

    std::unique_ptr<Eg::AprEntity> makeLine(double x, double y = 0.0)
    {
        auto line = std::make_unique<Eg::AprLine>();
        line->setPointVector({ Ut::Vec2d(x, y), Ut::Vec2d(x + 2.0, y + 2.0) });
        return line;
    }

    /// 假读取器：只认 .dxf，读取行为由用例注入（可在 worker 线程上被调用）
    class FakeReader : public IImportReader
    {
    public:
        using Body = std::function<ImportResult(Fio::VecAprEntityPtr&)>;

        explicit FakeReader(Body body)
            : m_body(std::move(body))
        {
        }

        Fio::FileFormat format() const override
        {
            return Fio::FileFormat::DXF;
        }
        QStringList supportedExtensions() const override
        {
            return { QStringLiteral("dxf") };
        }
        ImportDimension dimension() const override
        {
            return ImportDimension::Dim2D;
        }
        QString formatName() const override
        {
            return QStringLiteral("FakeDxf");
        }
        ImportResult read(const ImportContext& /*context*/, Fio::VecAprEntityPtr& outEntities) override
        {
            return m_body ? m_body(outEntities) : ImportResult::fail(QStringLiteral("no body"));
        }

    private:
        Body m_body;
    };

    /// 返回 n 条有效线段的读取行为
    FakeReader::Body linesBody(int n)
    {
        return [n](Fio::VecAprEntityPtr& out) {
            for (int i = 0; i < n; ++i)
            {
                out.push_back(makeLine(i * 10.0));
            }
            return ImportResult::ok(QStringLiteral("ok"), n);
        };
    }

    /// 真实导入链的最小装配：Dispatcher（假 DXF + 原生 .apr）→ ImportService → 场景 / 图层 / 编辑服务
    struct Harness
    {
        Eg::SceneManager scene;
        LayerManager layers{ &scene };
        HardwareProfileManager profiles;
        UndoRedoManager undo{ &scene };
        SceneEditService edit{ &scene, &undo };
        ImportDispatcher dispatcher;
        ImportService service;
        QTemporaryDir dir;
        QString dxfPath;

        explicit Harness(FakeReader::Body body)
        {
            layers.setHardwareProfileManager(&profiles);
            edit.setLayerManager(&layers);
            dispatcher.registerReader(std::make_unique<FakeReader>(std::move(body)));
            dispatcher.registerReader(std::make_unique<NativeImportReader>());
            service.setDispatcher(&dispatcher);
            service.setSceneManager(&scene);
            service.setLayerManager(&layers);
            service.setEditService(&edit);

            // phaseDetectFormat 要求文件存在；内容由假读取器决定，留空即可
            dxfPath = dir.filePath(QStringLiteral("input.dxf"));
            QFile file(dxfPath);
            if (file.open(QIODevice::WriteOnly))
            {
                file.close();
            }
        }

        void seedExistingEntity()
        {
            // addEntity 内部克隆入场，入参所有权仍归调用方
            const auto line = makeLine(-1000.0);
            Eg::SceneWriteGateway::addEntity(scene, line.get());
        }

        static ImportOptions options(bool replaceDocument)
        {
            ImportOptions opts;
            opts.autoFit = false;
            opts.autoCenter = false;
            opts.autoSwitchWorkbench = false;
            opts.importAsNewDocument = replaceDocument;
            return opts;
        }
    };

    /// 写一份单图层原生 .apr，图层显示属性由参数给出（带硬件 Profile 值集）
    bool writeSingleLayerArchive(const QString& path, const std::string& layerName, const Ut::Color& color,
        bool visible, bool locked)
    {
        Eg::SceneManager scene;
        LayerManager layers(&scene);
        HardwareProfileManager profiles;
        layers.setHardwareProfileManager(&profiles);

        const int layerId = layers.createLayer(layerName);
        if (layerId < 0)
        {
            return false;
        }
        if (!layers.setLayerColor(layerId, color))
        {
            return false;
        }
        layers.setLayerVisible(layerId, visible);
        layers.setLayerLocked(layerId, locked);

        Eg::AprPolygon polygon;
        polygon.setVertices({ Ut::Vec2d(0.0, 0.0), Ut::Vec2d(10.0, 0.0), Ut::Vec2d(10.0, 10.0), Ut::Vec2d(0.0, 10.0) });
        polygon.bClosed = true;
        Eg::SceneWriteGateway::addEntity(scene, &polygon);
        Eg::AprEntity* entity = scene.getAllEntities().back();
        entity->id = 4242;
        // 先分配再锁层：锁定图层可能拒绝接收图元
        layers.setLayerLocked(layerId, false);
        if (!layers.assignEntityToLayer(entity, layerId))
        {
            return false;
        }
        layers.setLayerLocked(layerId, locked);

        NativeDocumentSnapshot snapshot = aprSetupCaptureMachiningSnapshot(&layers);
        Fio::AprDocument doc;
        for (const auto& layer : snapshot.layers)
        {
            doc.addLayer(layer);
        }
        for (const auto& profile : snapshot.profiles)
        {
            doc.addHardwareProfile(profile);
        }
        for (Eg::AprEntity* e : scene.getAllEntities())
        {
            doc.addBorrowedEntity(e);
        }

        Fio::AprSerializer serializer;
        return serializer.saveToFile(path.toUtf8().constData(), doc, false, Fio::FileFormat::Native).success;
    }

    bool isReddish(const Ut::Color& c)
    {
        return c.red() > 0.9f && c.blue() < 0.1f;
    }
    bool isBluish(const Ut::Color& c)
    {
        return c.blue() > 0.9f && c.red() < 0.1f;
    }

    /// 协调器用例的场景：图元必须挂在可见图层上才会进入几何账本（isDesiredOnGeometryLayer）
    struct RefreshScene
    {
        Eg::SceneManager scene;
        LayerManager layers{ &scene };
        SceneEditService edit{ &scene, nullptr };

        RefreshScene()
        {
            edit.setLayerManager(&layers);
        }

        /// 在 (originX, originY) 起按 100 列网格铺 count 条短线
        void addGrid(int count, double originX, double originY)
        {
            std::vector<std::unique_ptr<Eg::AprEntity>> entities;
            entities.reserve(static_cast<size_t>(count));
            for (int i = 0; i < count; ++i)
            {
                entities.push_back(makeLine(originX + (i % 100) * 10.0, originY + (i / 100) * 10.0));
            }
            edit.addEntities(std::move(entities), "seed", CommitMode::Direct);
        }
    };

    /// 把 RenderWidget 的相机缩小到 1/factor（可视世界盒放大 factor 倍）
    void zoomOutWidget(RenderWidget& widget, float factor)
    {
        // viewMatrix() / setViewMatrix() 都按 backing size 换算，成对使用不受 DPR 影响
        Render::Mat3f m = widget.viewMatrix();
        m.data[0] /= factor;
        m.data[4] /= factor;
        m.data[6] /= factor;
        m.data[7] /= factor;
        widget.setViewMatrix(m);
    }
}  // namespace ImportFailurePath

// ============================================================================
// ImportService：替换文档的事务边界
// ============================================================================

TEST(ImportFailurePathTest, ReplaceDocument_ParseFailure_LeavesDocumentUntouched)
{
    using namespace ImportFailurePath;
    Harness h([](Fio::VecAprEntityPtr&) {
        return ImportResult::fail(QStringLiteral("boom"), ImportErrorType::ParseFailed);
    });
    h.seedExistingEntity();

    int replaceCalls = 0;
    ImportContext context;
    context.sourcePath = h.dxfPath;
    context.beforeReplaceDocumentCallback = [&replaceCalls]() { ++replaceCalls; };

    const ImportResult result = h.service.importWithContext(context, Harness::options(true));

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.documentCleared);
    EXPECT_EQ(replaceCalls, 0) << "undo stack must not be cleared when nothing is going to replace the document";
    EXPECT_EQ(h.scene.getEntityCount(), 1u) << "current document was cleared by a failed open";
}

TEST(ImportFailurePathTest, ReplaceDocument_AllEntitiesRejected_LeavesDocumentUntouched)
{
    using namespace ImportFailurePath;
    Harness h([](Fio::VecAprEntityPtr& out) {
        out.push_back(nullptr);
        out.push_back(nullptr);
        return ImportResult::ok(QStringLiteral("ok"), 2);
    });
    h.seedExistingEntity();

    int replaceCalls = 0;
    ImportContext context;
    context.sourcePath = h.dxfPath;
    context.beforeReplaceDocumentCallback = [&replaceCalls]() { ++replaceCalls; };

    const ImportResult result = h.service.importWithContext(context, Harness::options(true));

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.documentCleared);
    EXPECT_EQ(replaceCalls, 0);
    EXPECT_EQ(h.scene.getEntityCount(), 1u);
}

TEST(ImportFailurePathTest, ReplaceDocument_CallsReplaceCallbackBeforeClearingScene)
{
    using namespace ImportFailurePath;
    Harness h(linesBody(3));
    h.seedExistingEntity();

    int replaceCalls = 0;
    size_t entitiesWhenCalled = 0;
    ImportContext context;
    context.sourcePath = h.dxfPath;
    context.beforeReplaceDocumentCallback = [&]() {
        ++replaceCalls;
        entitiesWhenCalled = h.scene.getEntityCount();
    };

    const ImportResult result = h.service.importWithContext(context, Harness::options(true));

    ASSERT_TRUE(result.success) << result.message.toStdString();
    EXPECT_TRUE(result.documentCleared);
    EXPECT_EQ(replaceCalls, 1);
    EXPECT_EQ(entitiesWhenCalled, 1u) << "callback must run before clearScene (old document still present)";
    EXPECT_EQ(h.scene.getEntityCount(), 3u) << "old entity must be gone, new ones in";
    EXPECT_EQ(result.entityCount, 3);
}

TEST(ImportFailurePathTest, MergeImport_NeverCallsReplaceCallback)
{
    using namespace ImportFailurePath;
    Harness h(linesBody(3));
    h.seedExistingEntity();

    int replaceCalls = 0;
    ImportContext context;
    context.sourcePath = h.dxfPath;
    context.beforeReplaceDocumentCallback = [&replaceCalls]() { ++replaceCalls; };

    const ImportResult result = h.service.importWithContext(context, Harness::options(false));

    ASSERT_TRUE(result.success) << result.message.toStdString();
    EXPECT_FALSE(result.documentCleared);
    EXPECT_EQ(replaceCalls, 0);
    EXPECT_EQ(h.scene.getEntityCount(), 4u);
}

// 落库完成后才到达的取消请求：图元已入场不可回滚，必须按成功收尾（刷新 + 状态回写都要执行）
TEST(ImportFailurePathTest, CancelRequestedAfterEntitiesLanded_ImportStillSucceeds)
{
    using namespace ImportFailurePath;
    Harness h(linesBody(3));

    QString writtenBackPath;
    ImportContext context;
    context.sourcePath = h.dxfPath;
    context.cancelCallback = [&h]() { return h.scene.getEntityCount() > 0; };
    context.currentDocumentPathCallback = [&writtenBackPath](const QString& path) { writtenBackPath = path; };

    const ImportResult result = h.service.importWithContext(context, Harness::options(false));

    EXPECT_TRUE(result.success) << result.message.toStdString();
    EXPECT_NE(result.errorType, ImportErrorType::Canceled);
    EXPECT_EQ(h.scene.getEntityCount(), 3u);
    EXPECT_EQ(writtenBackPath, h.dxfPath) << "write-back phase was skipped";
}

// ============================================================================
// ImportService：重入保护
// ============================================================================

TEST(ImportFailurePathTest, ImportRefusedWhileAsyncImportInFlight_AndAcceptedAfterwards)
{
    using namespace ImportFailurePath;
    ensureApplication();

    // 第一次导入的读取器阻塞在 worker 线程上，直到用例放行
    auto gate = std::make_shared<std::promise<void>>();
    std::shared_future<void> released = gate->get_future().share();
    auto entered = std::make_shared<std::atomic<bool>>(false);
    auto firstCall = std::make_shared<std::atomic<bool>>(true);

    Harness h([released, entered, firstCall](Fio::VecAprEntityPtr& out) {
        if (firstCall->exchange(false))
        {
            entered->store(true);
            released.wait();
        }
        out.push_back(makeLine(0.0));
        return ImportResult::ok(QStringLiteral("ok"), 1);
    });

    // 无论断言在哪里失败都要放行 worker，否则 ImportService 析构时 join 会挂住整个测试进程
    bool gateOpened = false;
    auto openGate = [&]() {
        if (!gateOpened)
        {
            gateOpened = true;
            gate->set_value();
        }
    };
    struct GateGuard
    {
        std::function<void()> open;
        ~GateGuard()
        {
            open();
        }
    } guard{ openGate };

    ImportContext context;
    context.sourcePath = h.dxfPath;

    bool firstDone = false;
    ImportResult firstResult;
    h.service.importAsync(context, Harness::options(false), [&](const ImportResult& r) {
        firstResult = r;
        firstDone = true;
    });
    ASSERT_TRUE(pumpUntil([&]() { return entered->load(); }, 3000)) << "worker never reached the reader";

    // 同步导入：立即拒绝，不得在主线程上等上一次解析
    const auto begin = std::chrono::steady_clock::now();
    const ImportResult refusedSync = h.service.importWithContext(context, Harness::options(false));
    const auto elapsedMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - begin).count();
    EXPECT_FALSE(refusedSync.success);
    EXPECT_LT(elapsedMs, 500) << "re-entrant import blocked the main thread for " << elapsedMs << " ms";

    // 异步导入：同样拒绝，且结果经事件循环投递（调用方此时才连上自己的等待循环）
    bool secondDone = false;
    ImportResult secondResult;
    h.service.importAsync(context, Harness::options(false), [&](const ImportResult& r) {
        secondResult = r;
        secondDone = true;
    });
    EXPECT_FALSE(secondDone) << "refusal must be queued, not delivered synchronously";
    ASSERT_TRUE(pumpUntil([&]() { return secondDone; }, 3000));
    EXPECT_FALSE(secondResult.success);
    EXPECT_FALSE(firstDone) << "refusing the second import must not finish the first one";

    // 放行第一次导入：它应正常完成
    openGate();
    ASSERT_TRUE(pumpUntil([&]() { return firstDone; }, 5000)) << "first import never finished";
    EXPECT_TRUE(firstResult.success) << firstResult.message.toStdString();

    // 收尾后重入保护解除：新的导入可以进行
    const ImportResult after = h.service.importWithContext(context, Harness::options(false));
    EXPECT_TRUE(after.success) << "in-flight flag was not released: " << after.message.toStdString();
    EXPECT_EQ(h.scene.getEntityCount(), 2u);
}

// ============================================================================
// ImportService：打开 .apr 按文件重建图层表；追加导入 .apr 按颜色落层
// ============================================================================

TEST(ImportFailurePathTest, OpenNativeDocument_RebuildsLayerTableFromFile)
{
    using namespace ImportFailurePath;
    Harness h(linesBody(0));
    const QString path = h.dir.filePath(QStringLiteral("doc.apr"));
    ASSERT_TRUE(writeSingleLayerArchive(path, "Cut", Ut::Color::fromRGB255(250, 10, 10), true, false));

    // 上一份文档留下的图层：同名 Cut（蓝、隐藏、锁定）和一个文件里没有的 Stale
    const int stale = h.layers.createLayer("Cut");
    ASSERT_GE(stale, 0);
    ASSERT_TRUE(h.layers.setLayerColor(stale, Ut::Color::fromRGB255(10, 10, 250)));
    h.layers.setLayerVisible(stale, false);
    h.layers.setLayerLocked(stale, true);
    ASSERT_GE(h.layers.createLayer("Stale"), 0);

    ImportContext context;
    context.sourcePath = path;
    const ImportResult result = h.service.importWithContext(context, Harness::options(true));
    ASSERT_TRUE(result.success) << result.message.toStdString();

    EXPECT_LT(h.layers.findLayerByName("Stale"), 0) << "layer of the previous document survived opening";
    const int cut = h.layers.findLayerByName("Cut");
    ASSERT_GE(cut, 0);
    EXPECT_TRUE(h.layers.isLayerVisible(cut)) << "visibility leaked from the previous document";
    EXPECT_FALSE(h.layers.isLayerLocked(cut)) << "lock state leaked from the previous document";
    EXPECT_TRUE(isReddish(h.layers.getLayerColor(cut))) << "color leaked from the previous document";
    EXPECT_EQ(h.layers.getLayerEntities(cut).size(), 1u);
    EXPECT_EQ(h.layers.currentLayerId(), 0);
}

TEST(ImportFailurePathTest, AppendNativeDocument_LandsOnSameColorLayer)
{
    using namespace ImportFailurePath;
    Harness h(linesBody(0));
    const QString path = h.dir.filePath(QStringLiteral("doc.apr"));
    ASSERT_TRUE(writeSingleLayerArchive(path, "Cut", Ut::Color::fromRGB255(250, 10, 10), true, false));

    // 当前文档已有同色图层（名字不同、隐藏）
    const int mine = h.layers.createLayer("Mine");
    ASSERT_GE(mine, 0);
    ASSERT_TRUE(h.layers.setLayerColor(mine, Ut::Color::fromRGB255(250, 10, 10)));
    h.layers.setLayerVisible(mine, false);

    ImportContext context;
    context.sourcePath = path;
    const ImportResult result = h.service.importWithContext(context, Harness::options(false));
    ASSERT_TRUE(result.success) << result.message.toStdString();

    // 图层 = 颜色：不按名新建 Cut，图元落到同色的 Mine，Mine 的设置不被源文件覆盖
    EXPECT_LT(h.layers.findLayerByName("Cut"), 0);
    EXPECT_EQ(h.layers.getLayerEntities(mine).size(), 1u);
    EXPECT_FALSE(h.layers.isLayerVisible(mine));
}

// ============================================================================
// SceneRefreshCoordinator：全量提交失败与视口扩展升级
// ============================================================================

TEST(RefreshFailurePathTest, FullRefreshSubmitFailure_KeepsFullLevelAndRetries)
{
    using namespace ImportFailurePath;
    ensureApplication();

    RefreshScene rs;
    rs.addGrid(10, 0.0, 0.0);

    RenderWidget widget;
    widget.resize(800, 600);

    SceneRefreshCoordinator coordinator;
    coordinator.setRenderWidget(&widget);
    coordinator.setSceneContext(&rs.scene);
    coordinator.setRenderReadyPredicate([]() { return true; });

    int attempts = 0;
    bool submitSucceeds = false;
    coordinator.setFullSubmitCallback([&](Eg::SceneManager*) {
        ++attempts;
        return submitSucceeds;
    });

    coordinator.requestFullRefresh();
    coordinator.flushPendingRefresh();
    EXPECT_EQ(attempts, 1);
    EXPECT_EQ(coordinator.pendingLevel(), UI::SceneRefreshLevel::FullRefresh)
        << "a failed full submit must not be recorded as built";

    // 定时器自动重试，失败期间级别保持全量
    EXPECT_TRUE(pumpUntil([&]() { return attempts >= 2; }, 1000)) << "failed full refresh was never retried";
    EXPECT_EQ(coordinator.pendingLevel(), UI::SceneRefreshLevel::FullRefresh);

    // 上下文恢复：下一次重试成功后级别清零
    submitSucceeds = true;
    EXPECT_TRUE(pumpUntil([&]() { return coordinator.pendingLevel() == UI::SceneRefreshLevel::None; }, 1000));

    coordinator.stop();
}

TEST(RefreshFailurePathTest, ViewportExtensionAboveThreshold_EscalatesToFullRefresh)
{
    using namespace ImportFailurePath;
    ensureApplication();

    // 17000 > kBulkChangeThreshold(16384)，全部放在默认视口之外
    RefreshScene rs;
    rs.addGrid(17000, 5000.0, 5000.0);

    RenderWidget widget;
    widget.resize(800, 600);

    SceneRefreshCoordinator coordinator;
    coordinator.setRenderWidget(&widget);
    coordinator.setSceneContext(&rs.scene);
    coordinator.setRenderReadyPredicate([]() { return true; });
    int fullSubmits = 0;
    coordinator.setFullSubmitCallback([&fullSubmits](Eg::SceneManager*) {
        ++fullSubmits;
        return true;
    });

    // 首次全量：锚定「已构建盒」为默认视口（盒内没有图元）
    coordinator.requestFullRefresh();
    coordinator.flushPendingRefresh();
    ASSERT_EQ(fullSubmits, 1);

    // 缩小 100 倍（等价于导入后 zoomToFit）：整份场景落进可视盒、且在已构建盒之外
    zoomOutWidget(widget, 100.0f);
    coordinator.requestViewportRefresh();

    EXPECT_EQ(fullSubmits, 2) << "large viewport extension must escalate to one full rebuild, "
                                 "not per-entity upserts";

    coordinator.stop();
}

TEST(RefreshFailurePathTest, ViewportExtensionBelowThreshold_StaysIncremental)
{
    using namespace ImportFailurePath;
    ensureApplication();

    RefreshScene rs;
    rs.addGrid(100, 5000.0, 5000.0);

    RenderWidget widget;
    widget.resize(800, 600);

    SceneRefreshCoordinator coordinator;
    coordinator.setRenderWidget(&widget);
    coordinator.setSceneContext(&rs.scene);
    coordinator.setRenderReadyPredicate([]() { return true; });
    int fullSubmits = 0;
    coordinator.setFullSubmitCallback([&fullSubmits](Eg::SceneManager*) {
        ++fullSubmits;
        return true;
    });
    // 增量补入在无 GL 时会失败并转入异步队列；隔离掉批次提交，只看是否误升级
    coordinator.setSubmitBatchCallback([](const std::vector<uint64_t>&) { return true; });

    coordinator.requestFullRefresh();
    coordinator.flushPendingRefresh();
    ASSERT_EQ(fullSubmits, 1);

    zoomOutWidget(widget, 100.0f);
    coordinator.requestViewportRefresh();

    EXPECT_EQ(fullSubmits, 1) << "small viewport extension must stay on the incremental path";

    coordinator.stop();
}
