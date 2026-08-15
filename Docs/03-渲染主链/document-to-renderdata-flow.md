# 文档到渲染数据流程

## 1. 目标

本文件描述文档数据如何转换成渲染数据，并最终进入 GPU renderer。

核心目标：
- 文档是事实源
- render data 是中间转译层
- GPU renderer 是最终绘制层
- viewport 只是承载与显示层

---

## 2. 总流程

```text
Document / Scene
→ selection / 状态
→ render data
→ GPU renderer
→ viewport
→ screen
```

---

## 3. 各层职责

### 3.1 文档层

文档层保存：
- 图元
- selection
- 变换结果
- 提交结果
- 回滚状态

文档层不负责：
- 直接绘制
- GPU 资源管理
- 视口重绘策略

### 3.2 render data 层

render data 层负责：
- 提取文档中的图元信息
- 组织可绘制批次
- 组织 selection 高亮数据
- 组织预览数据

render data 层不负责：
- 修改文档
- 处理用户输入
- 执行操作调度

### 3.3 GPU renderer 层

GPU renderer 负责：
- 接收 render data
- 编译绘制帧
- 执行 GPU 绘制
- 输出最终屏幕结果

GPU renderer 不负责：
- 文档状态真相
- selection 真相
- operation 调度

### 3.4 viewport 层

viewport 负责：
- 接收刷新请求
- 接收输入事件
- 承载渲染上下文
- 触发 redraw / present

viewport 不负责：
- 文档修改
- render data 生成
- 几何算法

---

## 4. 推荐转换流程

### Step 1

文档状态发生变化，例如：
- 提交
- 取消
- selection 变化
- 预览变化

### Step 2

文档层触发刷新通知。

### Step 3

`SceneEditServiceAdapter` 或 `ViewWidget` 进行中间同步：
- 同步 selection
- 更新 render data

### Step 4

GPU renderer 接收新数据并绘制。

### Step 5

viewport 触发重绘并显示结果。

---

## 5. 推荐职责拆分

### 5.1 `SceneEditServiceAdapter`

负责在提交、取消、预览时推动文档状态变化，并触发刷新回调。

### 5.2 `GpuSceneRenderer2D`

负责 Main → Render2D 格式翻译，生成可绘制的渲染帧。

### 5.3 `Render2DSink`

负责将渲染帧投递到 GPU RenderWidget。

### 5.4 `ViewWidget`（UI/2D 模块）

负责：
- 工具切换（通过 ToolManager）
- 鼠标事件分发（通过 ToolInputDispatcher）
- 渲染协调（通过 ViewRenderCoordinator）
- 生命周期管理

---

## 6. 工具切换链路

当前系统使用单工具系统（UI/2D 模块），切换路径如下：

```
按钮点击
  → OperationBus::run(OperationId::Tool_Line)
    → ToolSwitchOperation::execute()
      → ViewWidget::setActiveTool("LineTool")
        → ToolManager::setActiveTool()
          → oldTool->onDeactivate()
          → newTool->onActivate()
```

工具定义在 `UI/2D/Src/Ui/DrawTools/` 目录下，所有工具实现 `ITool` 接口，通过 `ToolManager` 统一管理。

---

## 6. selection 处理规则

selection 在渲染链中的角色是事实源的可视化映射。

### 规则
- selection 必须从文档同步
- selection 高亮应由 render data 生成
- viewport 不应保存第二份 selection 真相

---

## 7. 预览数据处理

预览数据和正式数据要分开。

### 规则
- 预览数据只影响临时显示
- 正式提交才进入最终状态
- 取消后预览数据必须清理

---

## 8. 需要避免的情况

- 文档层直接调用绘制 API
- render data 和文档状态混成一份
- viewport 自己维护 selection 真相
- 预览和提交共用同一份最终数据
- GPU renderer 直接改文档

---

## 9. 验证点

### 提交
- 提交后 render data 更新
- GPU renderer 绘制新状态
- selection 高亮同步

### 取消
- 取消后回到原状态
- 预览痕迹清除
- viewport 重绘正确

### 预览
- 预览过程可连续刷新
- 临时状态不污染正式结果

---

## 10. 结论

文档到渲染数据的关键，不是把对象直接画出来，而是把文档事实稳定地转成 render data，再交给 GPU renderer 绘制。

---

## 11. 数据层与渲染层解耦方案（2026-07-15 更新）

### 11.1 设计目标

通过接口解耦实现数据层和渲染层的完全分离，便于未来更换渲染后端，同时保持类型安全和编译时检查。

### 11.2 核心接口

#### ISceneGeometrySink（渲染层实现）

定义几何原语接收接口，由渲染层实现，用于接收数据层推送的几何数据。

```cpp
class ISceneGeometrySink
{
public:
    virtual ~ISceneGeometrySink() = default;
    
    virtual void emitPolyline(const std::vector<Ut::Vec2d>& points, bool bClosed) = 0;
    virtual void emitCircle(const Ut::Vec2d& center, double radius) = 0;
    virtual void emitArc(const Ut::Vec2d& center, double radius,
                         double startAngle, double endAngle) = 0;
    virtual void emitEllipse(const Ut::Vec2d& center,
                             double radiusX, double radiusY,
                             double rotation,
                             double startAngle, double endAngle,
                             bool bFullEllipse) = 0;
    virtual void emitText(const Ut::Vec2d& position, const std::string& text) = 0;
    virtual void emitImagePlaceholder(const Ut::Vec2d& topLeft,
                                      const Ut::Vec2d& topRight,
                                      const Ut::Vec2d& bottomLeft,
                                      const Ut::Vec2d& bottomRight) = 0;
    virtual void emitTriangleSoup(const std::vector<Ut::Vec3f>& vertices,
                                  const std::vector<Ut::Vec3f>& normals) = 0;
};
```

#### ISceneDataSource（数据层实现）

定义只读场景视图接口，由数据层实现，渲染器通过它获取场景几何数据。

```cpp
class ISceneDataSource
{
public:
    virtual ~ISceneDataSource() = default;
    
    virtual void gatherGeometry(ISceneGeometrySink& sink) const = 0;
};
```

### 11.3 架构图

```
┌─────────────────┐     ISceneDataSource     ┌─────────────────┐
│   数据层         │ ───────────────────────► │   适配层         │
│ (Engine2D)      │                          │ (RenderCompat)   │
│ SceneManager    │                          │ SceneGeometry    │
└─────────────────┘                          │ SinkAdapter      │
                                             └────────┬────────┘
                                                      │ C API
                                                      ▼
                                             ┌─────────────────┐
                                             │   渲染层         │
                                             │  (Renderx)      │
                                             │  SanYiRender.dll│
                                             └─────────────────┘
```

### 11.4 各层职责

| 层级 | 组件 | 职责 | 关键文件 |
|------|------|------|----------|
| **数据层** | `SceneManager` | 继承 `ISceneDataSource`，遍历图元并推送几何原语 | `Engine2D/Core/SceneManager.h/cpp` |
| **适配层** | `SceneGeometrySinkAdapter` | 实现 `ISceneGeometrySink`，将接口调用转换为 C API 调用 | `RenderCompat/SceneGeometrySinkAdapter.h/cpp` |
| **渲染层** | `SanYiRender.dll` | 提供几何原语推送的 C API，实现原语细分和渲染 | `Renderx/c_api/render_c_api.cpp` |

### 11.5 支持的图元类型

`SceneManager::gatherGeometry` 支持以下图元类型的几何推送：

| 图元类型 | 推送方法 |
|----------|----------|
| LINE / POLYLINE | `emitPolyline` |
| CIRCLE | `emitCircle` |
| ARC | `emitArc` |
| ELLIPSE | `emitEllipse` |
| POLYGON | `emitPolyline`（闭合） |
| TEXT | `emitText` |
| IMAGE | `emitImagePlaceholder`（四角点） |
| POINT | `emitPolyline`（单点） |
| BEZIER（三阶） | `emitPolyline`（4控制点） |
| BEZIER2（二阶） | `emitPolyline`（3控制点） |
| NURBS | `emitPolyline`（控制点序列） |
| SMARTLINE | `emitPolyline`（各段基点） |

### 11.6 架构优势

| 优势 | 说明 |
|------|------|
| **低耦合** | 数据层和渲染层完全解耦，通过接口通信，互不依赖具体实现 |
| **可替换性** | 更换渲染后端只需实现新的 `ISceneGeometrySink` 适配器，无需修改数据层 |
| **类型安全** | 通过 C++ 接口定义保证数据传递的类型正确性，编译时检查 |
| **增量更新** | 配合 RenderWorld 增量更新机制，支持高效的场景渲染 |
| **跨语言边界** | 通过 C API 封装，支持不同语言编写的渲染后端 |

### 11.7 渲染流程（2026-08-15 更新）

#### 场景数据提交流程

```
场景变化 → SceneRefreshCoordinator（16ms 节流，分级刷新）
    ├─→ RefreshLevel::Repaint      → 仅 renderWidget->update()（纯视觉重绘）
    ├─→ RefreshLevel::LightUpdate  → applyLightRefresh()（增量提交脏图元）
    └─→ RefreshLevel::FullRefresh  → applyFullRefresh()
            │
            └─→ RenderWidget::submitSceneFromDataSource(dataSource)
                    ├─→ renderBeginScene(m_device)
                    │       // 清除所有旧图元（RenderWorld::clearAllEntities），
                    │       // 避免新数据源场景与旧图元累积冲突
                    ├─→ SceneGeometrySinkAdapter sink(m_device)
                    ├─→ dataSource->gatherGeometry(sink)
                    │       │
                    │       └─→ SceneManager::gatherGeometry()
                    │               │
                    │               ├─→ sink.emitPolyline(...)  → renderEmitPolyline → world2D.addEntity()
                    │               ├─→ sink.emitCircle(...)    → renderEmitCircle    → tessellateCircle → addEntity()
                    │               ├─→ sink.emitArc(...)       → renderEmitArc       → tessellateArc → addEntity()
                    │               └─→ sink.emitEllipse(...)   → renderEmitEllipse   → tessellateEllipse → addEntity()
                    ├─→ renderEndScene(m_device)
                    └─→ QOpenGLWidget::update()   // 触发 paintGL
```

#### 每帧渲染流程

```
paintGL() → renderFrame()
    ├─→ rhi->beginFrame()
    │
    ├─→ 数据准备（Pass 外）：
    │       ├─→ syncWorldToPersistentManager() + executeCulling()（GPU 剔除）
    │       ├─→ readBackGpuVisibility()（可见性回读，失效时回退 CPU 四叉树）
    │       └─→ batchQueue.submit()（按 PrimitiveType 排序组装批次）
    │
    ├─→ RenderGraph Pass 编排（renderGraph.execute）：
    │       ├─→ Pass 0 FrameSetup（清屏、混合、重置命令编码器）
    │       ├─→ Pass 1 SceneEnv（网格背景）
    │       ├─→ Pass 2 Bitmap（位图）
    │       ├─→ Pass 3 World2DCollect（world2D 命令 → CommandEncoder）
    │       ├─→ Pass 4 OverlayCollect（overlay 命令 → CommandEncoder）
    │       ├─→ Pass 5 CommandExecute（统一排序执行，经 PSM 绑定管线）
    │       └─→ Pass 6 Text（文本，条件启用）
    │
    └─→ rhi->endFrame() / present()
```

> 说明：早期版本（2026-07-16）记录的"world2D.update → queryVisible → sceneEnv.render →
> batchQueue.submit/render → overlayQueue.render → meshManager.render"线性流程已被
> RenderGraph 显式 Pass 编排 + GPU 剔除取代，详情见 `渲染管线.md` Phase 4 与
> `viewport-refresh-flow.md` §10。

#### 着色器映射机制

(`rhi_gl.cpp`)

```cpp
// 着色器名称 → GLSL 源码映射表（使用 std::string 键，确保字符串比较）
static const std::unordered_map<std::string, const char*> shaderMap = {
    {"passthrough_vert", shader::SCENE_2D_VERT},
    {"passthrough_frag", shader::SCENE_2D_FRAG},
    {"overlay_vert",     shader::OVERLAY_VERT},
    {"overlay_frag",     shader::OVERLAY_FRAG},
    // ... 其他着色器
};

// 调用方可通过着色器名称（如 "passthrough_vert"）引用，
// 也可直接传入着色器源码（如 shader::MESH_3D_INSTANCED_VERT）
// 映射表查找失败时，回退为直接使用传入字符串作为源码
```

### 11.8 更换渲染后端步骤

1. 实现新的 `ISceneGeometrySink` 适配器
2. 在适配器中调用新渲染后端的 API
3. 链接新的渲染库（替换 SanYiRender.dll）
4. 无需修改 `SceneManager` 或其他数据层代码

---

## 12. 命令链路状态（2026-07-11 更新）

### 12.1 命令处理器拆分

原 `UiCommandHandler.cpp`（约 2700 行）已拆分为多个独立文件：

| 文件 | 职责 |
|------|------|
| `CreateCommands.h/cpp` | 创建命令（DrawLine、Circle、Arc、Polyline、Polygon） |
| `TransformCommands.h/cpp` | 变换命令（Move、Rotate、Copy、Delete、Mirror） |
| `SelectCommands.h/cpp` | 选择命令（Select） |

### 12.2 文档绑定

**文件**：`ApplicationCompositionRoot.h/cpp`

**完成内容**：
- 添加 `SceneDocument2D` 成员变量
- 创建文档对象并设置到 `UiServices.document2D`
- 所有命令处理器的 `activate()` 方法添加 `m_document = services.document2D`

### 12.3 命令链路状态确认

| 工具 | 主链状态 | 旧路径清理 | 预览 | 提交 | Undo/Redo | 刷新 |
|------|----------|-----------|------|------|-----------|------|
| Select | ✅ | ✅ | N/A | ✅ | N/A | ✅ |
| Line | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Polyline | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Circle | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Arc | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Polygon | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Move | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Rotate | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Copy | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Delete | ✅ | ✅ | N/A | ✅ | ✅ | ✅ |
| Mirror | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |

---

## 13. Main 模块目录结构

### 13.1 UI 子目录

```text
Main/Src/UI/
├── Adapters/          # 适配器类
│   ├── SceneEditServiceAdapter.h/cpp   # 文档编辑服务适配器
│   └── RenderWidget3DAdapter.h/cpp    # 3D 渲染控件适配器
├── Documents/         # 文档类
│   ├── EntityDocument2D.h/cpp         # 2D 图元文档
│   ├── SceneDocument3D.h/cpp          # 3D 场景文档
│   ├── SceneTreeModel2D.h             # 2D 场景树数据模型（纯数据）
│   ├── SceneTreeBuilder2D.h/cpp       # 2D 场景树构建器（算法层）
│   ├── SceneTreeModel3D.h             # 3D 场景树数据模型（纯数据）
│   └── SceneTreeBuilder3D.h/cpp       # 3D 场景树构建器（算法层）
├── Entities/          # 图元类
│   ├── UiEntity.h                     # 图元基类
│   ├── ITransformable.h               # 可变换接口
│   ├── LineEntity2D.h/cpp             # 2D 线图元
│   ├── PolylineEntity2D.h/cpp         # 2D 多段线图元
│   ├── CircleEntity2D.h/cpp           # 2D 圆图元
│   ├── ArcEntity2D.h/cpp              # 2D 圆弧图元
│   ├── SceneNode.h/cpp                # 3D 场景节点
│   ├── SelectionSet.h/cpp             # 选择集
│   └── CameraController3D.h/cpp       # 3D 相机控制器
├── Render/            # 渲染相关
│   ├── SimpleRenderer3D.h/cpp         # 3D 软件渲染器
│   ├── GpuSceneRenderer2D.h/cpp       # 2D GPU 场景渲染桥接器
│   ├── IRenderDataSink.h              # 渲染数据接收器接口
│   └── Render2DSink.h/cpp             # 2D 渲染 sink
├── Services/          # 服务类
│   ├── UiServices.h                   # UI 服务集合
│   ├── UiFrameworkServices.h          # 框架服务集合
│   ├── UiStateCenter.h/cpp            # 状态中心
│   ├── UiLayoutService.h/cpp          # 布局服务
│   ├── UiThemeService.h/cpp           # 主题服务
│   ├── UiShellHost.h/cpp              # Shell 主机
│   └── UiSelectionTools.h/cpp         # 选择工具
├── Widgets/           # UI 控件
│   ├── Viewport3D.h/cpp               # 3D 视口
│   ├── UiSceneTreePanel2D.h/cpp       # 2D 场景树控件（数据/算法/UI 分离）
│   ├── UiSceneTreePanel3D.h/cpp       # 3D 场景树控件（数据/算法/UI 分离）
│   ├── PropertiesPanelWidget.h/cpp    # 属性面板控件
│   └── WorkbenchWindow.h/cpp          # 工作台窗口
├── Workbench/         # 工作台类
│   ├── UiWorkbench.h/cpp              # 工作台基类
│   ├── Workbench2D.h/cpp              # 2D 工作台
│   └── Workbench3D.h/cpp              # 3D 工作台
├── UiViewWidgets.h    # 视图控件聚合头（保持向后兼容）
└── UiCommandDispatcher.h # 命令分发器
```

### 13.2 UI/2D 模块工具目录

```text
UI/2D/Src/Ui/DrawTools/
├── ITool.h                           # 工具接口
├── BaseTool.h/cpp                    # 工具基类（状态机 + 预览）
├── ToolContext.h                     # 工具上下文（IoC 注入）
├── ToolManager.h/cpp                 # 工具管理器
├── SelectTool.h/cpp                  # 选择工具
├── LineTool.h/cpp                    # 直线工具
├── CircleTool.h/cpp                  # 圆工具
├── ArcTool.h/cpp                     # 圆弧工具
├── PolyLineTool.h/cpp                # 多段线工具
├── RectangleTool.h/cpp               # 矩形工具
├── EllipseTool.h/cpp                 # 椭圆工具
├── TriangleTool.h/cpp                # 三角形工具
├── PolygonTool.h/cpp                 # 多边形工具
├── SmartLineTool.h/cpp               # 智能线工具
├── BezierTool.h/cpp                  # 贝塞尔曲线工具
├── SplineTool.h/cpp                  # 样条曲线工具
├── NurbsTool.h/cpp                   # NURBS 曲线工具
├── TextInputTool.h/cpp               # 文本输入工具
└── ...                               # 更多工具
```

### 13.3 Include 路径约定

为了避免使用 `../` 相对路径，CMakeLists.txt 已将所有 UI 子目录加入 include 路径。代码中直接使用头文件名引用即可：

```cpp
// 推荐方式（已采用）
#include "UiServices.h"
#include "SceneEditServiceAdapter.h"

// 避免方式（已清理）
#include "../UiServices.h"
#include "../Adapters/SceneEditServiceAdapter.h"
```

### 13.4 关键文件职责映射

| 文件 | 职责 | 渲染链角色 |
|------|------|-----------|
| `SceneEditServiceAdapter` | 文档编辑事务管理 | 文档层 → render data 层 |
| `GpuSceneRenderer2D` | Main → Render2D 格式翻译 | render data 层 → GPU renderer |
| `Render2DSink` | GPU 渲染帧投递 | GPU renderer → viewport 层 |
| `SceneCompiler` | 场景编译 | 文档层 → render data 层 |
| `ViewWidget` (UI/2D) | 工具管理 + 事件分发 + 渲染协调 | viewport 层 |
| `ToolManager` (UI/2D) | 工具注册与切换 | 无（输入分发） |
| `OperationBus` (UI/2D) | 操作分发总线 | 无（命令调度） |
