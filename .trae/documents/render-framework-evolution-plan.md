# 渲染框架演进计划

## Context

当前渲染框架已具备基础架构：SceneCompiler 三层抽象（SceneTraverser + CompilationStrategy + BatchManager）、RenderCoreRenderer 桥接层、Backend 工厂与配置体系、UiShellHost/Workbench3D 宿主层。测试覆盖约 120 个用例。

需要解决的问题：
1. **增量编译路径未实际启用**：RenderCoreRenderer::compileScene() 始终调用全量 compile()，增量编译路径仅在测试中直接调用 compileIncremental() 验证，未在真实渲染链路中走通
2. **场景切换缓存失效不完整**：切换场景时未显式调用 invalidateCache()，依赖全量编译路径兜底
3. **RenderCoreRenderer 边界可进一步收紧**：渲染入口不统一，脏标记判断逻辑分散
4. **Backend 配置测试覆盖不完整**：缺少 unknown backend fallback、环境变量覆写等边界测试
5. **UiShellHost/Workbench3D 闭包可简化**

---

## 第一优先级：稳定渲染编译层

### 任务 1.1：验证 SceneCompiler 缓存一致性

**修改文件**：
- `Main/Src/RenderCore/RenderCoreRenderer.cpp` — 通过 context 判断走增量/全量编译
- `Main/Src/RenderCore/CompilationStrategy.h` — 新增 `isCacheInvalidated()` 查询
- `Main/Src/UI/Test/RenderCoreTests.cpp` — 新增缓存一致性测试

**具体改动**：

1. **RenderCoreRenderer::compileScene()** 改为增量编译优先：
   ```cpp
   RenderFrame RenderCoreRenderer::compileScene() {
       if (!m_document) return {};
       m_context.advanceFrame();
       if (m_context.isDirty || !m_compiler->hasCachedFrame()) {
           // 全量编译
           m_compiler->invalidateCache();
           RenderFrame frame = m_compiler->compile(m_document, m_context);
           m_context.clearDirty();
           return frame;
       }
       // 增量编译（缓存命中）
       RenderFrame previous = m_compiler->hasCachedFrame() ? RenderFrame{} : RenderFrame{};
       auto frame = m_compiler->compileIncremental(m_document, m_context,
           m_compiler->hasCachedFrame() ? m_compiler->compile(m_document, m_context) : RenderFrame{});
       // 修正：需要从 compiler 获取缓存帧
       m_context.clearDirty();
       return frame;
   }
   ```
   
   实际实现更简洁：
   ```cpp
   RenderFrame RenderCoreRenderer::compileScene() {
       if (!m_document) return {};
       m_context.advanceFrame();
       RenderFrame frame;
       if (m_context.isDirty) {
           m_compiler->invalidateCache();
           frame = m_compiler->compile(m_document, m_context);
       } else {
           frame = m_compiler->compile(m_document, m_context); // 内部自动判断增量
       }
       m_context.clearDirty();
       return frame;
   }
   ```
   
   核心改动：将增量编译决策完全交给 SceneCompiler 内部，compile() 方法内部判断是否可走增量路径。

2. **DefaultSceneCompiler::compile()** 内部增加增量路径判断：
   - 若 `m_strategy.cacheValid() && !m_strategy.forceFullCompile() && !context.isDirty` → 走增量
   - 否则走全量

3. **新增测试**（在 RenderCoreTests.cpp 中）：
   - `SceneCompilerTest, IncrementalCompile_DeleteEntityCachePurged` — 删除实体后增量编译不残留旧批次
   - `SceneCompilerTest, IncrementalCompile_SceneSwitchCacheRebuilt` — 场景切换 + invalidateCache 后缓存重建
   - `SceneCompilerTest, IncrementalCompile_CacheHitAfterMultipleDirty` — 多次脏更新后缓存命中正确
   - `SceneCompilerTest, IncrementalCompile_NoPhantomBatches` — 增量编译后批次数量与全量编译一致

**验收标准**：
- 增量编译结果与全量编译结果等价（batchCount、entityCount、vertexCount 一致）
- 删除实体后增量编译不残留旧批次
- 场景切换 + invalidateCache 后缓存可重建
- 缓存命中时 compileTimeMs ≈ 0

---

### 任务 1.2：拆分 SceneCompiler 职责边界

**当前状态**：DefaultSceneCompiler 已通过三层架构拆分（SceneTraverser + CompilationStrategy + BatchManager），但 SceneCompiler 接口仍暴露 batch query 方法（groupBatchesByPrimitiveType、cullBatches）。

**修改文件**：
- `Main/Src/RenderCore/SceneCompiler.h` — 移除 batch query 方法，保持纯编译接口
- `Main/Src/RenderCore/BatchQuery.h`（新增）— 独立的批次查询接口
- `Main/Src/RenderCore/BatchManager.h` — 实现 BatchQuery 接口
- `Main/Src/RenderCore/DefaultSceneCompiler.h` — 移除 batch query 委托
- `Main/Src/RenderCore/DefaultSceneCompiler.cpp` — 移除相关实现
- `Main/Src/UI/Test/RenderCoreTests.cpp` — 更新测试引用

**具体改动**：

1. 创建 `BatchQuery` 独立接口：
   ```cpp
   class BatchQuery {
   public:
       virtual ~BatchQuery() = default;
       virtual QVector<int> groupByPrimitiveType(const RenderFrame& frame) const = 0;
       virtual RenderFrame cullByViewport(const RenderFrame& frame, const QRectF& viewportRect) const = 0;
   };
   ```

2. BatchManager 实现 BatchQuery 接口

3. SceneCompiler 接口精简为纯编译+缓存控制+脏追踪

4. 调用方改为直接使用 BatchManager（或通过 RenderCoreRenderer 暴露）

**验收标准**：
- SceneCompiler 接口仅包含编译、缓存、脏追踪方法
- 批次查询通过独立接口访问
- 现有测试全部通过

---

## 第二优先级：收紧 RenderCoreRenderer

### 任务 2.1：验证 RenderCoreRenderer 桥接纯度

**当前状态**：RenderCoreRenderer 已基本是纯桥接层，但 compileScene() 中做了增量/全量编译决策，这部分逻辑应下沉到 SceneCompiler。

**修改文件**：
- `Main/Src/RenderCore/RenderCoreRenderer.cpp` — 简化 compileScene()，移除编译策略判断

**具体改动**：

1. compileScene() 简化为：
   ```cpp
   RenderFrame RenderCoreRenderer::compileScene() {
       if (!m_document) return {};
       m_context.advanceFrame();
       auto frame = m_compiler->compile(m_document, m_context);
       m_context.clearDirty();
       return frame;
   }
   ```

2. 所有编译策略判断（增量/全量/缓存命中）由 SceneCompiler 内部完成

3. 审计 render() 方法：确认无业务逻辑泄漏到桥接层

**验收标准**：
- compileScene() 不超过 5 行逻辑
- 编译决策完全在 SceneCompiler 内部
- RenderCoreRenderer 不做任何 if/else 编译策略判断

---

### 任务 2.2：统一渲染请求入口

**当前状态**：渲染触发路径分散在 render()、resize()、resetView()、onMouseMove() 等多个方法中，各自通过 m_context.markDirty() 触发重编译。

**修改文件**：
- `Main/Src/RenderCore/RenderCoreRenderer.cpp` — 提取统一的 requestRender() 调度入口

**具体改动**：

1. 新增私有方法 `scheduleRender()`：
   ```cpp
   void RenderCoreRenderer::scheduleRender() {
       m_context.markDirty();
   }
   ```

2. 所有触发重渲染的地方统一调用 `scheduleRender()`：
   - resize() → scheduleRender()
   - resetView() → scheduleRender()
   - onMouseMove() → scheduleRender()
   - onWheel() → scheduleRender()
   - setScene() → scheduleRender()

3. render() 作为唯一渲染执行入口，内部调用 compileScene() → SoftwareRenderer::render()

**验收标准**：
- 渲染触发路径统一为 scheduleRender() → render() → compileScene() → SoftwareRenderer::render()
- 无直接调用 compile() 或手动设置 dirty 的散落代码
- 重复渲染和漏渲染问题消除

---

## 第三优先级：整理 Backend 工厂和配置

### 任务 3.1：验证配置切换逻辑

**修改文件**：
- `Main/Src/UI/Test/RenderCoreTests.cpp` — 补充配置切换边界测试

**新增测试**：
- `RenderBackendFactoryTest, EnvironmentVariable_Software` — SAN_YI_RENDER_BACKEND=software 生效
- `RenderBackendFactoryTest, EnvironmentVariable_UnknownFallback` — 未知后端名回退到默认
- `RenderBackendFactoryTest, ConfigResolve_AllTypes` — 遍历 OpenGL/Vulkan/Metal/Software 四种类型
- `RenderBackendFactoryTest, CapabilityConsistency` — 工厂创建的后端能力与注册表一致

**验收标准**：
- 不改 UI 即可通过环境变量切换 backend
- 未知 backend 名安全回退到平台默认
- 后端选择可预测、可复现

---

### 任务 3.2：后端能力标记整理

**当前状态**：BackendCapability 枚举已定义（HardwareAccelerated、MultiViewport、InstancedRendering 等），BackendCapabilityRegistry 已注册各后端能力。

**修改文件**：
- `Main/Src/RenderCore/BackendCapabilityRegistry.h` — 新增查询方法
- `Main/Src/RenderCore/BackendCapabilityRegistry.cpp` — 实现查询方法

**具体改动**：

1. 新增查询方法：
   ```cpp
   bool supportsRenderMode(BackendType type, RenderMode mode) const;
   QStringList supportedRenderModes(BackendType type) const;
   QStringList supportedInputModes(BackendType type) const;
   bool supportsFrameCapture(BackendType type) const;
   ```

2. 后端能力结构化：将 render mode / input mode / frame capture 支持信息纳入 BackendInfo

**验收标准**：
- 后端能力可查询、可比较、可选择
- 支持按 render mode 筛选后端
- 支持按 capability 筛选后端

---

## 第四优先级：继续瘦 UI 宿主和工作台

### 任务 4.1：再收 UiShellHost

**修改文件**：
- `Main/Src/UI/UiShellHost.cpp` — 简化 initializeAndShow() 中的闭包

**具体改动**：

1. 将 `initializeAndShow()` 中的 themeChangeCallback 闭包提取为独立方法 `onThemeChanged(const QString& themeId)`
2. 将 workbenchFactory 闭包提取为独立方法（已存在 resolveWorkbench）
3. 减少 lambda 捕获：themeChangeCallback 只捕获 this

**验收标准**：
- initializeAndShow() 中无闭包定义
- 成员数量不增加
- 每个方法职责单一

---

### 任务 4.2：再收 Workbench3D

**修改文件**：
- `Main/Src/UI/UiWorkbench.h` — 提取 SceneBuilder3D 辅助类声明
- `Main/Src/UI/UiWorkbench.cpp` — 提取场景构建逻辑

**具体改动**：

1. 提取 `SceneBuilder3D` 辅助类（cpp 内部或独立文件）：
   - 负责场景节点创建（Root/Mesh/Child A/Child B）
   - 负责选择状态初始化
   - 返回构建好的 SceneDocument3D

2. Workbench3D::build3DWorkbenchUi() 简化为：
   ```cpp
   void Workbench3D::build3DWorkbenchUi(WorkbenchWindow& window) {
       m_scene = SceneBuilder3D::buildDefaultScene();
       auto* properties = buildPropertiesPanel(window);
       auto* sceneDock = buildSceneTree(window, properties);
       buildToolBars(window);
       window.setCentralWidget(buildViewport(window, properties, sceneDock));
       init3DInitialState(*m_scene, m_scene->rootNodes().first()->id());
   }
   ```

3. 选择回调 `onSceneTreeSelection` 收口：减少参数传递，通过成员变量访问

**验收标准**：
- build3DWorkbenchUi() 不超过 10 行
- 场景构建、面板构建、视口构建各自独立
- 选择回调不通过闭包捕获外部变量

---

## 第五优先级：测试体系工程化

### 任务 5.1：分类测试

**当前状态**：测试已分两个文件（RenderCoreTests.cpp 单元测试 + FrameworkIntegrationTests.cpp 集成测试），但未按三层分类。

**修改文件**：
- `Main/Src/UI/Test/RenderCoreTests.cpp` — 重组为三个 section
- `Main/Src/UI/Test/FrameworkIntegrationTests.cpp` — 补充生命周期测试

**重组方案**：

RenderCoreTests.cpp（核心单元测试）：
- Section 1: Compiler（SceneCompiler 全量/增量/缓存/脏标记/批次分组/裁剪）
- Section 2: Backend（DefaultRenderBackend 生命周期/上下文/渲染模式/帧提交）
- Section 3: Context（RenderContext 脏标记/帧号/场景类型）
- Section 4: Camera（UiCamera3D 默认状态/轨道/平移/缩放/投影/输入）

FrameworkIntegrationTests.cpp（集成测试）：
- Section 1: 生命周期（startup / switch / shutdown / idempotent close）
- Section 2: 渲染链路（input → compile → render / dirty update / multi-frame）
- Section 3: 配置切换（环境变量 / 后端切换 / 能力查询）

**验收标准**：
- 测试按三层分类清晰可辨
- 每个 section 有明确的注释分隔
- 新增测试放入正确分类

---

### 任务 5.2：控制测试粒度

**原则**：
- 小逻辑（标记脏、帧号递增、字符串转换）→ 单测
- 框架行为（编译→缓存→增量→渲染）→ 集成测试
- 真正渲染行为（像素级验证）→ 端到端测试（预留，当前不需要）

**验收标准**：
- 单元测试不依赖 QPainter/QImage
- 集成测试不验证像素级输出
- 测试运行时间可控（< 1 秒）

---

## 第六优先级：准备 GPU 后端演进

### 任务 6.1：统一 OpenGL/Vulkan/Metal 接入规则

**当前状态**：IRenderBackend 接口已定义完整生命周期（initialize/shutdown/compile/submitFrame/render/beginFrame/endFrame/captureFrame）。

**修改文件**：
- `Main/Src/RenderCore/IRenderBackend.h` — 补充接口文档，明确接入规则
- `Main/Src/RenderCore/BackendAccessRules.md`（新增）— 接入规则文档

**具体改动**：

1. 在 IRenderBackend.h 中补充接入规则注释：
   - backend 如何初始化（nativeWindowHandle 传递规则）
   - context 如何传递（bindContext 时机）
   - render frame 如何提交（compile → submitFrame → render 顺序）
   - resize 如何处理（viewportSize 更新 + 资源重建）
   - capture 如何处理（软件后端直接返回，GPU 后端通过 glReadPixels）

2. 确保 IRenderBackend 接口足够通用，不包含任何 OpenGL 特定概念

**验收标准**：
- 接口文档完整，新后端开发者可仅凭接口文档实现
- 接口中无 OpenGL 特定类型/概念泄漏

---

### 任务 6.2：为真实 GPU 后端做准备

**当前状态**：RenderTypes.h 中顶点数据结构（RenderVertex）已预留 3D 坐标（x/y/z + r/g/b/a），适合 GPU 传输。

**修改文件**：
- `Main/Src/RenderCore/RenderTypes.h` — 补充 GPU 传输友好的数据结构注释
- `Main/Src/RenderCore/RenderFrame.h` — 确保帧结构可序列化

**具体改动**：

1. 确认 RenderVertex 内存布局适合 GPU（7 个 float，28 字节，对齐良好）
2. 确认 RenderBatch 可转换为 GPU DrawCall
3. 补充注释标记 GPU 传输路径

**验收标准**：
- 数据结构可直接用于 glBufferData / vkMapMemory 等 GPU API
- 不引入新的抽象层，保持现有数据结构
- 后续引入真实 GPU 后端时 UI 层无需修改

---

## Verification

### 构建验证
```bash
cd c:\Users\xx\Documents\Cpp\CAD
cmake --build build --target RenderCoreTests --config Debug
cmake --build build --target FrameworkIntegrationTests --config Debug
```

### 测试验证
```bash
cd build
ctest -R RenderCoreTests --output-on-failure
ctest -R FrameworkIntegrationTests --output-on-failure
```

### 关键回归检查点
1. 所有现有测试通过（约 120 个用例）
2. 新增测试覆盖增量编译边界场景
3. SceneCompiler 接口精简后无编译错误
4. RenderCoreRenderer 编译决策下沉后渲染行为不变
5. 环境变量切换后端功能正常