/**
 * @file main.cpp
 * @brief Metal 原生视口原型：验证 Qt 宿主能否把 NSView 交给 RenderX 的 Metal 后端
 *
 * ## 这个原型要回答的问题
 *
 * 产品的 2D 视口（UI/2D 的 RenderWidget）是 QOpenGLWidget，渲染器创建还绑在
 * initializeGL 上（"GL 函数指针必须在有当前上下文时解析"）。而 Metal 需要的
 * 是 `NativeWindowKind::CocoaNsView` + 真正的 NSView*，并且 MetalDevice 的
 * createSurface 只接受这一种窗口类型。QOpenGLWidget 的 NSView 由 Qt 与 GL
 * 上下文共同掌管，**不能**在其上挂 CAMetalLayer —— 所以必须先把视口变成
 * 普通原生窗口。本原型把两条候选路径各跑一遍，用实际表现决定选哪条：
 *
 *   A) QWidget + Qt::WA_NativeWindow —— winId() 直接就是 NSView*
 *   B) 非 GL 的 QWindow + QWidget::createWindowContainer —— winId() 也是 NSView*
 *
 * ## 观察点
 *
 *   1. Metal 的 createSurface 是否接受该 NSView（拿不到 CAMetalLayer 会失败）
 *   2. nextDrawable 能否取到后备缓冲（窗口/layer 尺寸不对时返回 nil，
 *      表现为 rxSessionBeginFrame 失败 —— 这是本原型最主要的失败模式）
 *   3. resize 之后能否恢复出帧
 *   4. Qt 控件叠在原生视口之上是否还能正常合成（两侧各放一个半透明标签）
 *
 * ## 自检模式
 *
 * `--frames N` 渲染 N 帧后自动退出，退出码 0 = 所有视口都成功初始化并且
 * 每帧都提交成功、且没有非预期的 DLL error。这样它既能让人看着跑，也能
 * 无人值守地跑出可判定的结果。
 *
 * 用法：
 *   MetalViewportProto                  # 并排打开 A/B 两个窗口，持续渲染
 *   MetalViewportProto --frames 120     # 各渲染 120 帧后退出
 *   MetalViewportProto --mode=a|b|both  # 只跑其中一条路径
 */

#include "render/renderx.h"

#include <QApplication>
#include <QLabel>
#include <QSize>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <QWindow>
#include <QResizeEvent>
#include <QShowEvent>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
    // ======================================================================
    // 日志：收集 RenderX 交回来的日志，供自检判定
    // ======================================================================

    struct LogSink
    {
        int errors = 0;
        int warnings = 0;
        /// 仅记录 error 级文本，用于自检时区分「已知缺口」与「新问题」
        std::vector<std::string> errorMessages;

        static LogSink& instance()
        {
            static LogSink sink;
            return sink;
        }

        static void callback(Render::RT::LogLevel level, const char* message, void* userData)
        {
            auto* sink = static_cast<LogSink*>(userData);
            if (sink == nullptr)
            {
                return;
            }
            const char* text = message ? message : "";
            if (level == Render::RT::LogLevel::Error)
            {
                sink->errors += 1;
                sink->errorMessages.emplace_back(text);
            }
            else if (level == Render::RT::LogLevel::Warn)
            {
                sink->warnings += 1;
            }
        }

        /**
         * @brief 已知缺口：与本原型要验证的问题无关的 error
         *
         * mesh_3d 的两个 shader 属 M5、culling.comp 属 M4/M6，它们会在
         * Runtime::create 时报 error。若不排除，自检会永远失败，失去信号价值。
         */
        static bool isKnownGap(const std::string& text)
        {
            return text.find("mesh_3d_p3n3") != std::string::npos ||
                   text.find("Mesh3D") != std::string::npos ||
                   text.find("culling.comp") != std::string::npos;
        }

        /// 非已知缺口的 error 条数
        int unexpectedErrors() const
        {
            int count = 0;
            for (const std::string& text : errorMessages)
            {
                if (!isKnownGap(text))
                {
                    count += 1;
                }
            }
            return count;
        }
    };

    constexpr uint32_t kFontPixelHeight = 28;

    /// 屏幕空间 P3C3 顶点（像素坐标 + 顶点色）
    struct ScreenVertex
    {
        float x, y, z;
        float r, g, b;
    };
    static_assert(sizeof(ScreenVertex) == 24, "P3C3 步长必须为 24");

    /// 屏幕空间 P2T2C4 顶点（像素坐标 + UV + 颜色）
    struct GlyphVertex
    {
        float x, y;
        float u, v;
        float r, g, b, a;
    };
    static_assert(sizeof(GlyphVertex) == 32, "P2T2C4 步长必须为 32");

    // ======================================================================
    // RenderCore：一个视口的 runtime / surface / session + 每帧绘制
    // ======================================================================

    class RenderCore
    {
    public:
        bool initialize(void* nativeView, uint32_t width, uint32_t height);
        void shutdown();

        /// 准备文字：查字形 + 上传图集 + 排版。由宿主在 initialize 之后调用一次。
        bool buildTextQuads(const char* text);

        /// 返回本帧是否成功提交（BeginFrame 与 EndFrame 都 Ok）
        bool renderFrame();
        bool resize(uint32_t width, uint32_t height);

        uint32_t framesRendered() const { return m_frames; }
        uint32_t framesFailed() const { return m_failures; }
        uint32_t resizeCalls() const { return m_resizeCalls; }
        uint32_t resizeFailures() const { return m_resizeFailures; }
        const std::string& lastError() const { return m_lastError; }
        const std::string& deviceName() const { return m_deviceName; }
        bool isReady() const { return Render::RT::rxValid(m_session); }

    private:
        bool submitTriangle(float phase);

        Render::RT::RuntimeHandle m_runtime{ Render::RT::RuntimeHandle::Invalid };
        Render::RT::SurfaceHandle m_surface{ Render::RT::SurfaceHandle::Invalid };
        Render::RT::SessionHandle m_session{ Render::RT::SessionHandle::Invalid };
        Render::RT::FontHandle m_font{ Render::RT::FontHandle::Invalid };
        Render::RT::TextureHandle m_atlas{ Render::RT::TextureHandle::Invalid };
        uint16_t m_glyphPipeline = 0;

        /// 已排好版的字形（图集 UV + 像素位置），初始化时算一次
        struct PlacedGlyph
        {
            float x0, y0, x1, y1;
            float u0, v0, u1, v1;
        };
        std::vector<PlacedGlyph> m_glyphs;

        uint32_t m_width = 0;
        uint32_t m_height = 0;
        uint32_t m_frames = 0;
        uint32_t m_failures = 0;
        uint32_t m_resizeCalls = 0;
        uint32_t m_resizeFailures = 0;
        std::string m_lastError;
        std::string m_deviceName;
    };

    bool RenderCore::initialize(void* nativeView, uint32_t width, uint32_t height)
    {
        if (nativeView == nullptr)
        {
            m_lastError = "winId() 没有给出原生视图（NSView* 为空）";
            return false;
        }

        Render::RT::RuntimeDesc rd{};
        rd.abiVersion = RENDERX_ABI_VERSION;
        rd.backend = Render::RT::Backend::Metal;
        rd.enableValidation = 0;
        rd.transientBufferBytes = 16ull * 1024 * 1024;
        rd.logCallback = &LogSink::callback;
        rd.logUserData = &LogSink::instance();
        rd.applicationName = "MetalViewportProto";
        m_runtime = Render::RT::rxRuntimeCreate(&rd);
        if (!Render::RT::rxValid(m_runtime))
        {
            m_lastError = "rxRuntimeCreate(Metal) 失败";
            return false;
        }

        Render::RT::Capabilities caps{};
        if (Render::RT::rxRuntimeGetCapabilities(m_runtime, &caps) == Render::RT::RxResult::Ok)
        {
            m_deviceName = caps.deviceName;
        }

        // 关键一步：把这个原生视图交给 Metal，由后端在其上挂 CAMetalLayer
        Render::RT::SurfaceDesc sd{};
        sd.windowKind = Render::RT::NativeWindowKind::CocoaNsView;
        sd.handleA = nativeView;
        sd.handleB = nullptr;
        sd.presentMode = Render::RT::PresentMode::Fifo;
        sd.width = width;
        sd.height = height;
        sd.enableDepth = 0;
        m_surface = Render::RT::rxSurfaceCreate(m_runtime, &sd);
        if (!Render::RT::rxValid(m_surface))
        {
            m_lastError = "rxSurfaceCreate(CocoaNsView) 失败（见上面的 DLL 日志）";
            shutdown();
            return false;
        }

        Render::RT::SessionDesc ses{};
        ses.runtime = m_runtime;
        ses.surface = m_surface;
        ses.clearColor[0] = 0.10f;
        ses.clearColor[1] = 0.12f;
        ses.clearColor[2] = 0.16f;
        ses.clearColor[3] = 1.0f;
        m_session = Render::RT::rxSessionCreate(&ses);
        if (!Render::RT::rxValid(m_session))
        {
            m_lastError = "rxSessionCreate 失败";
            shutdown();
            return false;
        }

        m_width = width;
        m_height = height;

        // 屏幕字形管线必须显式指定：它与 ScreenTextured 的（格式, 空间, 拓扑）
        // 三元组完全相同，让 Runtime 自行解析会命中位图管线，把 R8 覆盖率当
        // RGBA 采样，结果是纯红色的字。
        m_glyphPipeline =
            Render::RT::rxPipelineGetDefault(m_runtime, Render::RT::DefaultPipeline::ScreenGlyph);
        if (m_glyphPipeline == 0)
        {
            m_lastError = "ScreenGlyph 管线不可用（Metal 上缺 screen_glyph_p2t2c4_frag.metallib？）";
            shutdown();
            return false;
        }
        return true;
    }

    bool RenderCore::buildTextQuads(const char* text)
    {
        // 字体字节由宿主读入注入：DLL 不做文件 IO
        std::ifstream file(PROTO_FONT_PATH, std::ios::binary);
        if (!file.good())
        {
            m_lastError = std::string("字体文件打不开：") + PROTO_FONT_PATH;
            return false;
        }
        const std::vector<char> fontBytes((std::istreambuf_iterator<char>(file)),
                                          std::istreambuf_iterator<char>());

        Render::RT::FontDesc fd{};
        fd.data = fontBytes.data();
        fd.dataBytes = fontBytes.size();
        fd.pixelHeight = static_cast<float>(kFontPixelHeight);
        fd.sdfPadding = 0;  // 覆盖率模式：屏幕文字用
        if (Render::RT::rxFontCreate(m_runtime, &fd, &m_font) != Render::RT::RxResult::Ok)
        {
            m_lastError = "rxFontCreate 失败";
            return false;
        }

        // 先整串查字形（懒光栅化进 CPU 侧图集影子），再统一 flush 一次，
        // 避免逐字符一次纹理上传。
        struct Pending
        {
            Render::RT::GlyphInfo info;
            float pen;
        };
        std::vector<Pending> pending;
        float pen = 0.0f;
        for (const char* p = text; *p != '\0'; ++p)
        {
            Render::RT::GlyphInfo info{};
            const Render::RT::RxResult got =
                Render::RT::rxFontGlyph(m_runtime, m_font, static_cast<uint32_t>(*p), &info);
            if (got != Render::RT::RxResult::Ok)
            {
                m_lastError = "rxFontGlyph 失败";
                return false;
            }
            pending.push_back({ info, pen });
            pen += info.advance;
        }
        if (Render::RT::rxFontFlushAtlas(m_runtime, m_font) != Render::RT::RxResult::Ok)
        {
            m_lastError = "rxFontFlushAtlas 失败";
            return false;
        }
        m_atlas = Render::RT::rxFontAtlas(m_runtime, m_font);
        if (!Render::RT::rxValid(m_atlas))
        {
            m_lastError = "rxFontAtlas 返回无效句柄";
            return false;
        }

        // 排版：字形位图左上角 = 笔位置 + bearing。左上原点，y 向下。
        const float originX = 12.0f;
        const float originY = 12.0f;
        const float baseline = originY + static_cast<float>(kFontPixelHeight) * 0.8f;
        for (const Pending& item : pending)
        {
            if (item.info.width <= 0.0f || item.info.height <= 0.0f)
            {
                continue;  // 空格等空白字符不产生四边形
            }
            const float gx0 = originX + item.pen + item.info.bearingX;
            const float gy0 = baseline + item.info.bearingY;
            m_glyphs.push_back({ gx0, gy0, gx0 + item.info.width, gy0 + item.info.height,
                                 item.info.u0, item.info.v0, item.info.u1, item.info.v1 });
        }
        return true;
    }

    bool RenderCore::resize(uint32_t width, uint32_t height)
    {
        if (!Render::RT::rxValid(m_surface) || width == 0 || height == 0)
        {
            return false;
        }
        m_width = width;
        m_height = height;
        m_resizeCalls += 1;
        // 交换链尺寸变了之后继续用旧尺寸出帧会拿不到 drawable，
        // 因此必须把新尺寸交给后端，并且**把结果记下来**——
        // 「帧还在出」并不能证明 resize 真的生效了。
        const bool ok = Render::RT::rxSurfaceResize(m_runtime, m_surface, width, height) ==
                        Render::RT::RxResult::Ok;
        if (!ok)
        {
            m_resizeFailures += 1;
        }
        return ok;
    }

    bool RenderCore::submitTriangle(float phase)
    {
        Render::RT::TransientAlloc alloc{};
        if (Render::RT::rxSessionAllocTransient(m_session, sizeof(ScreenVertex) * 3, &alloc) !=
            Render::RT::RxResult::Ok)
        {
            m_lastError = "rxSessionAllocTransient(三角形) 失败";
            return false;
        }

        // 绕视口中心转的三角形：画面在动，才能区分「每帧都在出帧」
        // 与「只出了一帧之后卡住」
        const float cx = static_cast<float>(m_width) * 0.5f;
        const float cy = static_cast<float>(m_height) * 0.5f;
        const float radius = (std::min)(static_cast<float>(m_width),
                                        static_cast<float>(m_height)) *
                             0.25f;
        auto* v = static_cast<ScreenVertex*>(alloc.cpuPtr);
        for (int i = 0; i < 3; ++i)
        {
            const float angle = phase + static_cast<float>(i) * 2.0943951f;  // 120°
            v[i].x = cx + std::cos(angle) * radius;
            v[i].y = cy + std::sin(angle) * radius;
            v[i].z = 0.0f;
            v[i].r = (i == 0) ? 0.95f : 0.20f;
            v[i].g = (i == 1) ? 0.85f : 0.25f;
            v[i].b = (i == 2) ? 0.95f : 0.30f;
        }

        Render::RT::DrawCommand command{};
        command.vertexBuffer = alloc.buffer;
        command.vertexOffset = alloc.offset;
        command.vertexCount = 3;
        command.topology = Render::RT::PrimitiveTopology::Triangles;
        command.space = Render::RT::RenderSpace::Screen;
        command.vertexFormat = Render::RT::VertexFormat::P3C3;
        command.indexType = Render::RT::IndexType::None;
        command.sortKey = Render::RT::rxMakeSortKey(20, 0, 0, 0);

        Render::RT::DrawPacket packet{};
        packet.commands = &command;
        packet.commandCount = 1;
        packet.enableCulling = 0;
        packet.viewMatrix[0] = 1.0f;
        packet.viewMatrix[5] = 1.0f;
        packet.viewMatrix[10] = 1.0f;
        packet.viewMatrix[15] = 1.0f;
        packet.viewport[0] = 0.0f;
        packet.viewport[1] = 0.0f;
        packet.viewport[2] = static_cast<float>(m_width);
        packet.viewport[3] = static_cast<float>(m_height);
        return Render::RT::rxSessionSubmit(m_session, &packet) == Render::RT::RxResult::Ok;
    }

    bool RenderCore::renderFrame()
    {
        if (!Render::RT::rxValid(m_session))
        {
            return false;
        }

        const Render::RT::RxResult began = Render::RT::rxSessionBeginFrame(m_session);
        if (began != Render::RT::RxResult::Ok)
        {
            // 最常见的失败就在这里：acquireNextImage 拿不到 drawable
            m_lastError = std::string("rxSessionBeginFrame 失败：") +
                          Render::RT::rxResultName(began);
            m_failures += 1;
            return false;
        }

        // 1) 文字（先画，三角形压在上面更能看出层次）
        if (!m_glyphs.empty())
        {
            const size_t bytes = sizeof(GlyphVertex) * 6 * m_glyphs.size();
            Render::RT::TransientAlloc alloc{};
            if (Render::RT::rxSessionAllocTransient(m_session, bytes, &alloc) ==
                Render::RT::RxResult::Ok)
            {
                auto* v = static_cast<GlyphVertex*>(alloc.cpuPtr);
                size_t n = 0;
                for (const PlacedGlyph& g : m_glyphs)
                {
                    const GlyphVertex quad[6] = {
                        { g.x0, g.y0, g.u0, g.v0, 0.92f, 0.94f, 1.0f, 1.0f },
                        { g.x1, g.y0, g.u1, g.v0, 0.92f, 0.94f, 1.0f, 1.0f },
                        { g.x1, g.y1, g.u1, g.v1, 0.92f, 0.94f, 1.0f, 1.0f },
                        { g.x0, g.y0, g.u0, g.v0, 0.92f, 0.94f, 1.0f, 1.0f },
                        { g.x1, g.y1, g.u1, g.v1, 0.92f, 0.94f, 1.0f, 1.0f },
                        { g.x0, g.y1, g.u0, g.v1, 0.92f, 0.94f, 1.0f, 1.0f },
                    };
                    std::memcpy(v + n, quad, sizeof(quad));
                    n += 6;
                }

                Render::RT::DrawCommand command{};
                command.vertexBuffer = alloc.buffer;
                command.vertexOffset = alloc.offset;
                command.vertexCount = static_cast<uint32_t>(n);
                command.topology = Render::RT::PrimitiveTopology::Triangles;
                command.space = Render::RT::RenderSpace::Screen;
                command.vertexFormat = Render::RT::VertexFormat::P2T2C4;
                command.indexType = Render::RT::IndexType::None;
                command.texture = m_atlas;
                command.pipelineIndex = m_glyphPipeline;
                command.sortKey = Render::RT::rxMakeSortKey(10, 0, 0, 0);

                Render::RT::DrawPacket packet{};
                packet.commands = &command;
                packet.commandCount = 1;
                packet.enableCulling = 0;
                packet.viewMatrix[0] = 1.0f;
                packet.viewMatrix[5] = 1.0f;
                packet.viewMatrix[10] = 1.0f;
                packet.viewMatrix[15] = 1.0f;
                packet.viewport[2] = static_cast<float>(m_width);
                packet.viewport[3] = static_cast<float>(m_height);
                Render::RT::rxSessionSubmit(m_session, &packet);
            }
        }

        // 2) 三角形
        const float phase = static_cast<float>(m_frames) * 0.03f;
        if (!submitTriangle(phase))
        {
            m_failures += 1;
        }

        const Render::RT::RxResult ended = Render::RT::rxSessionEndFrame(m_session);
        if (ended != Render::RT::RxResult::Ok)
        {
            m_lastError = std::string("rxSessionEndFrame 失败：") +
                          Render::RT::rxResultName(ended);
            m_failures += 1;
            return false;
        }

        m_frames += 1;
        return true;
    }

    void RenderCore::shutdown()
    {
        if (Render::RT::rxValid(m_session))
        {
            Render::RT::rxSessionDestroy(m_session);
            m_session = Render::RT::SessionHandle::Invalid;
        }
        if (Render::RT::rxValid(m_font))
        {
            Render::RT::rxFontDestroy(m_runtime, m_font);
            m_font = Render::RT::FontHandle::Invalid;
            m_atlas = Render::RT::TextureHandle::Invalid;
        }
        if (Render::RT::rxValid(m_surface))
        {
            Render::RT::rxSurfaceDestroy(m_runtime, m_surface);
            m_surface = Render::RT::SurfaceHandle::Invalid;
        }
        if (Render::RT::rxValid(m_runtime))
        {
            Render::RT::rxRuntimeDestroy(m_runtime);
            m_runtime = Render::RT::RuntimeHandle::Invalid;
        }
    }

    // ======================================================================
    // MetalViewport：两种宿主方式的共同外壳
    // ======================================================================

    class MetalViewport : public QWidget
    {
    public:
        enum class Mode
        {
            NativeWidget,     ///< A) QWidget + WA_NativeWindow
            WindowContainer,  ///< B) QWindow + createWindowContainer
        };

        MetalViewport(Mode mode, const QString& label, QWidget* parent = nullptr)
            : QWidget(parent)
            , m_mode(mode)
            , m_label(label)
        {
            setMinimumSize(440, 340);
            setWindowTitle(label);

            auto* layout = new QVBoxLayout(this);
            layout->setContentsMargins(0, 0, 0, 0);
            layout->setSpacing(0);

            if (m_mode == Mode::WindowContainer)
            {
                // 非 GL 的 QWindow：它的 NSView 交给 Metal 挂 CAMetalLayer
                m_window = new QWindow();
                m_window->setTitle(label);
                QWidget* container = QWidget::createWindowContainer(m_window, this);
                container->setMinimumSize(200, 150);
                layout->addWidget(container, 1);
            }
            else
            {
                // A：让本控件自己成为原生窗口，winId() 即 NSView
                setAttribute(Qt::WA_NativeWindow, true);
                // 不让 Qt 自己画背景：整块区域归 Metal 的后备缓冲
                setAttribute(Qt::WA_OpaquePaintEvent, true);
                setAttribute(Qt::WA_NoSystemBackground, true);
                layout->addStretch(1);
            }

            // 叠在原生视口之上的 Qt 控件：用来观察合成是否正常。
            // 半透明 + 边框，能盖住说明 Qt 的合成没被 Metal 层吃掉。
            // 必须 WA_NativeWindow：Metal 画面在后端自建的子 QNSView（host）上，
            // 非原生子控件会被 Qt 光栅化进父 QNSView 的根 layer（位于子视图之下），
            // 只有原生兄弟 QNSView 才能排在 host 之上验证叠加层级。
            m_overlay = new QLabel(QStringLiteral("Qt overlay · ") + label, this);
            m_overlay->setAttribute(Qt::WA_NativeWindow, true);
            m_overlay->setStyleSheet(QStringLiteral(
                "background: rgba(255, 210, 90, 200); color: #201800; padding: 3px 8px;"
                "border-radius: 4px; font-weight: bold;"));
            m_overlay->adjustSize();
            m_overlay->raise();
        }

        ~MetalViewport() override
        {
            m_timer.stop();
            m_core.shutdown();
        }

        /// 开始逐帧渲染。由 main 在窗口显示后调用。
        void startRendering()
        {
            connect(&m_timer, &QTimer::timeout, this, [this]() { tick(); });
            m_timer.setInterval(16);
            m_timer.start();
            tick();  // 立即出一帧，不必等第一个 timeout
        }

        bool succeeded() const
        {
            return m_coreReady && m_core.framesFailed() == 0 && m_core.resizeFailures() == 0;
        }
        uint32_t frames() const { return m_core.framesRendered(); }
        uint32_t resizeCalls() const { return m_core.resizeCalls(); }
        uint32_t resizeFailures() const { return m_core.resizeFailures(); }
        const std::string& error() const { return m_core.lastError(); }
        const std::string& deviceName() const { return m_core.deviceName(); }
        const QString& label() const { return m_label; }

    protected:
        void showEvent(QShowEvent* event) override
        {
            QWidget::showEvent(event);
            // show 之后原生窗口才真正建立；下一轮事件循环再初始化
            QTimer::singleShot(0, this, [this]() { ensureCore(); });
        }

        void resizeEvent(QResizeEvent* event) override
        {
            QWidget::resizeEvent(event);
            placeOverlay();
            if (!m_coreReady)
            {
                ensureCore();
                return;
            }
            uint32_t w = 0;
            uint32_t h = 0;
            pixelSize(w, h);
            if (w != 0 && h != 0)
            {
                m_core.resize(w, h);
            }
        }

    private:
        /// 原生视口尺寸取「逻辑像素 × devicePixelRatio」，与产品 RenderWidget
        /// 的 backingSize 同一约定：Metal 表面的 drawableSize 是像素而不是点。
        void pixelSize(uint32_t& outWidth, uint32_t& outHeight) const
        {
            const double dpr = devicePixelRatio();
            const int w = static_cast<int>(width() * dpr);
            const int h = static_cast<int>(height() * dpr);
            outWidth = static_cast<uint32_t>(w > 0 ? w : 0);
            outHeight = static_cast<uint32_t>(h > 0 ? h : 0);
        }

        /// 原生视图：两条路径的差异只在这里
        void* nativeView()
        {
            if (m_mode == Mode::WindowContainer)
            {
                // winId() 会强制建立原生窗口
                return m_window != nullptr ? reinterpret_cast<void*>(m_window->winId()) : nullptr;
            }
            return reinterpret_cast<void*>(winId());
        }

        void ensureCore()
        {
            if (m_coreReady || !isVisible())
            {
                return;
            }
            uint32_t w = 0;
            uint32_t h = 0;
            pixelSize(w, h);
            if (w == 0 || h == 0)
            {
                return;  // 还没布局完，等下一次 resize
            }

            void* view = nativeView();
            if (view == nullptr)
            {
                m_coreFailed = true;
                std::fprintf(stderr, "[proto] %s: winId() 返回空，无法建 Metal 表面\n",
                             m_label.toUtf8().constData());
                return;
            }
            if (!m_core.initialize(view, w, h))
            {
                m_coreFailed = true;
                std::fprintf(stderr, "[proto] %s: 初始化失败：%s\n",
                             m_label.toUtf8().constData(), m_core.lastError().c_str());
                return;
            }
            if (!m_core.buildTextQuads(m_label.toUtf8().constData()))
            {
                m_coreFailed = true;
                std::fprintf(stderr, "[proto] %s: 文字准备失败：%s\n",
                             m_label.toUtf8().constData(), m_core.lastError().c_str());
                return;
            }
            m_coreReady = true;
            std::fprintf(stderr, "[proto] %s: 初始化成功（device=%s, %ux%u）\n",
                         m_label.toUtf8().constData(), m_core.deviceName().c_str(), w, h);
        }

        void tick()
        {
            if (!m_coreReady)
            {
                ensureCore();
                return;
            }
            if (!m_core.renderFrame())
            {
                // 只报第一次，避免刷屏
                if (m_core.framesFailed() == 1)
                {
                    std::fprintf(stderr, "[proto] %s: 出帧失败：%s\n",
                                 m_label.toUtf8().constData(), m_core.lastError().c_str());
                }
            }
        }

        void placeOverlay()
        {
            if (m_overlay == nullptr)
            {
                return;
            }
            m_overlay->move(12, 52);
            m_overlay->raise();
        }

        Mode m_mode;
        QString m_label;
        QWindow* m_window = nullptr;
        QLabel* m_overlay = nullptr;
        QTimer m_timer;
        RenderCore m_core;
        bool m_coreReady = false;
        bool m_coreFailed = false;
    };

    struct Options
    {
        int frames = 0;  ///< 0 = 一直渲染，不自动退出
        bool runA = true;
        bool runB = true;
        /// 实验开关：跳过控件删除，用来区分「删控件崩」与「进程退出崩」
        bool skipTeardown = false;
    };

    Options parseOptions(int argc, char** argv)
    {
        Options options{};
        for (int i = 1; i < argc; ++i)
        {
            const std::string arg = argv[i];
            if (arg == "--frames" && i + 1 < argc)
            {
                options.frames = std::atoi(argv[++i]);
            }
            else if (arg == "--no-teardown")
            {
                options.skipTeardown = true;
            }
            else if (arg.rfind("--mode=", 0) == 0)
            {
                const std::string mode = arg.substr(7);
                options.runA = (mode == "a" || mode == "both");
                options.runB = (mode == "b" || mode == "both");
            }
        }
        return options;
    }
}  // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    const Options options = parseOptions(argc, argv);

    if (Render::RT::rxIsBackendAvailable(Render::RT::Backend::Metal) == 0)
    {
        std::fprintf(stderr, "[proto] Metal 后端不可用，本原型无事可做\n");
        return 2;
    }

    std::vector<MetalViewport*> viewports;
    if (options.runA)
    {
        auto* a = new MetalViewport(MetalViewport::Mode::NativeWidget,
                                    QStringLiteral("A: QWidget + WA_NativeWindow"));
        a->setGeometry(60, 120, 620, 460);
        a->show();
        a->raise();
        a->activateWindow();  // macOS 下冒烟自检时把窗口带到前台便于目视
        viewports.push_back(a);
    }
    if (options.runB)
    {
        auto* b = new MetalViewport(MetalViewport::Mode::WindowContainer,
                                    QStringLiteral("B: QWindow + createWindowContainer"));
        b->setGeometry(720, 120, 620, 460);
        b->show();
        b->raise();
        b->activateWindow();
        viewports.push_back(b);
    }
    if (viewports.empty())
    {
        std::fprintf(stderr, "[proto] 没有选中任何模式\n");
        return 2;
    }

    // 等原生窗口与布局稳定后再开始出帧
    QTimer::singleShot(120, &app, [&viewports]() {
        for (MetalViewport* viewport : viewports)
        {
            viewport->startRendering();
        }
    });

    // 进度轮询：按**实际渲染帧数**判定，并在中途改一次窗口尺寸。
    // resize 是原生视口改造最容易坏的地方（交换链尺寸变了之后拿不到 drawable），
    // 因此自检必须覆盖它，而不是只跑一个固定尺寸。
    bool didResize = false;
    QTimer progress;
    progress.setInterval(50);
    QObject::connect(&progress, &QTimer::timeout, [&]() {
        uint32_t minFrames = 0xFFFFFFFFu;
        for (MetalViewport* viewport : viewports)
        {
            minFrames = (std::min)(minFrames, viewport->frames());
        }
        if (options.frames <= 0)
        {
            return;  // 不限帧数：一直渲染，只做上面那次 resize
        }
        if (!didResize && minFrames >= static_cast<uint32_t>(options.frames) / 3)
        {
            didResize = true;
            std::fprintf(stderr, "[proto] 改尺寸（验证 resize 后能否继续出帧）\n");
            for (MetalViewport* viewport : viewports)
            {
                const QSize current = viewport->size();
                viewport->resize(current.width() - 160, current.height() - 120);
            }
        }
        if (minFrames >= static_cast<uint32_t>(options.frames))
        {
            app.quit();
        }
    });
    progress.start();

    // 安全网：无论帧数多少都不许挂住
    QTimer::singleShot(60000, &app, [&app]() { app.quit(); });

    app.exec();

    // ---------------- 汇总 ----------------
    int exitCode = 0;
    std::fprintf(stderr, "\n===== 原型自检汇总 =====\n");
    if (options.frames > 0)
    {
        std::fprintf(stderr, "目标帧数 %d；中途执行过一次 resize —— 达到该帧数即证明 "
                             "resize 之后仍能持续拿到 drawable\n", options.frames);
    }
    for (MetalViewport* viewport : viewports)
    {
        const bool ok = viewport->succeeded();
        std::fprintf(stderr, "%-42s frames=%u resize=%u/%u %s%s\n",
                     viewport->label().toUtf8().constData(),
                     viewport->frames(),
                     viewport->resizeCalls() - viewport->resizeFailures(),
                     viewport->resizeCalls(),
                     ok ? "OK" : "FAILED",
                     ok ? "" : (": " + viewport->error()).c_str());
        if (!ok)
        {
            exitCode = 1;
        }
    }

    const LogSink& sink = LogSink::instance();
    std::fprintf(stderr, "DLL 日志：error=%d warn=%d（已知缺口：mesh_3d / culling 未实现）\n",
                 sink.errors, sink.warnings);
    const int unexpected = sink.unexpectedErrors();
    if (unexpected > 0)
    {
        std::fprintf(stderr, "非预期 error %d 条：\n", unexpected);
        for (const std::string& text : sink.errorMessages)
        {
            if (!LogSink::isKnownGap(text))
            {
                std::fprintf(stderr, "  %s\n", text.c_str());
            }
        }
        exitCode = 1;
    }

    // 两个视口都把 runtime 拆干净后再退出：泄漏会被 DLL 记 error。
    // --no-teardown 跳过删除，用于区分「删控件时崩」与「进程退出时崩」。
    if (!options.skipTeardown)
    {
        for (MetalViewport* viewport : viewports)
        {
            delete viewport;
        }
    }
    std::fprintf(stderr, "退出码=%d\n", exitCode);
    std::fflush(stderr);
    return exitCode;
}
