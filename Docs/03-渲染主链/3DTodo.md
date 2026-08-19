# 三维视图架构整改待办

## 目标

把当前 3D 视图收口为符合现有框架标准的正式链路。

目标原则：

- `Main` 只负责宿主与视图装配，不直接绑定具体渲染后端
- `UI3D` 负责 3D 工作台框架主层的菜单、工具栏、状态栏、命令总线、快捷键
- `RenderWidget3D` 作为旧 OpenGL 渲染兼容层保留
- `IRenderer3D` 作为唯一渲染抽象接口
- `IRenderer3D` 的创建必须有唯一来源，禁止在视图宿主中分散创建
- `SimpleRenderer3D` 仅保留为验证链或临时默认实现，不作为最终框架主链
- `Viewport3D` 作为统一宿主壳，负责承载 renderer，不承担后端选择职责

---

## 一、当前链路

### 1.1 Main/UI 层

当前核心文件：

- `Main/Src/UI/Render/UiViewport3D.h`
- `Main/Src/UI/Render/UiViewport3D.cpp`
- `Main/Src/UI/Render/SimpleRenderer3D.h`
- `Main/Src/UI/Render/SimpleRenderer3D.cpp`
- `Main/Src/UI/Render/RenderWidget3DAdapter.h`
- `Main/Src/UI/Render/RenderWidget3DAdapter.cpp`
- `Main/Src/UI/Render/Renderer3DFactory.h`
- `Main/Src/UI/Render/Renderer3DFactory.cpp`
- `Main/Src/UI/Workbench/Workbench3D.h`
- `Main/Src/UI/Workbench/Workbench3D.cpp`

当前要求：

- `Viewport3D` 通过 `IRenderer3D` 解耦渲染实现
- 默认 renderer 不应在视图宿主中散落创建
- 3D 视图装配应纳入组合根
- `Main` 侧不直接承担后端选择职责

### 1.2 UI3D 渲染控件层

当前核心文件：

- `UI/3D/Src/Render/RenderWidget3D.cpp`
- `Main/Src/UI/Render/RenderWidget3DAdapter.h`
- `Main/Src/UI/Render/RenderWidget3DAdapter.cpp`

当前要求：

- `RenderWidget3D` 作为旧 OpenGL 3D widget 保留兼容能力
- `RenderWidget3DAdapter` 作为过渡适配层保留
- 适配器只做事件转发、状态回调和桥接
- 不把适配器演化成第二套主框架

### 1.3 UI3D 层

当前核心文件：

- `UI/3D/Src/Ui/MainWindow/MainWindow3D.cpp`
- `UI/3D/Src/Ui/MainWindow/MainWindow3D.h`
- `UI/3D/Include/UI3D/Operation/OperationBus3D.h`
- `UI/3D/Include/UI3D/Operation/OperationDispatch3D.h`
- `UI/3D/Include/UI3D/Operation/CommandActionHub3D.h`
- `UI/3D/Include/UI3D/Operation/CommandRegistry3D.h`
- `UI/3D/Include/UI3D/Service/ServiceLocator3D.h`

当前要求：

- 3D 工作台框架骨架已存在
- 还需要和 `Main` 的视图链路统一
- 需要完整的“视图宿主 + 命令总线 + 渲染器注入”闭环
- `MainWindow3D::setupCentralWidget()` 不应再成为主链依赖点

---

## 二、当前架构结论

### 2.1 真实主链

当前 3D 视图的真实主链为：

- `Workbench3D`
- `Viewport3D`
- `IRenderer3D`
- 具体 renderer 由 `Renderer3DFactory` 创建并注入

### 2.2 `Viewport3D`

`Viewport3D` 当前职责：

- 承载 renderer
- 接收 UI 输入事件
- 转发给 renderer 接口
- 持有视图生命周期
- 通过抽象接口连接场景、相机、选择和路径

### 2.3 `RenderWidget3DAdapter`

`RenderWidget3DAdapter` 当前职责：

- 只做桥接
- 只维护兼容层行为
- 只同步状态，不编排业务
- 可连接场景、相机、选择、路径回调

---

## 三、当前约束

### 3.1 渲染器创建约束

#### 必须遵守
- 所有 `IRenderer3D` 实例必须由 `Renderer3DFactory` 创建
- `Viewport3D` 只能接收外部注入的 renderer
- `Workbench3D` 只能通过统一工厂获取 renderer

#### 禁止
- 在 UI 宿主层直接创建具体 renderer
- 在多个位置绕过工厂创建 renderer

### 3.2 `Viewport3D` 职责约束

#### 必须遵守
- `Viewport3D` 只负责宿主和事件转发
- `Viewport3D` 不负责选择渲染后端
- `Viewport3D` 不负责绑定业务逻辑

#### 禁止
- 在 `Viewport3D` 内部硬编码 renderer 类型
- 在 `Viewport3D` 内部直接操作引擎图元

### 3.3 `RenderWidget3DAdapter` 职责约束

#### 必须遵守
- 适配器只做桥接
- 适配器只维护兼容层行为
- 适配器只同步状态，不编排业务

#### 禁止
- 在适配器中加入新业务逻辑
- 把适配器演化成第二套主框架
- 让适配器承担未来主链职责

---

## 四、后续收口方向

- 继续统一 `Viewport3D` 与 `Renderer3DFactory` 的装配关系
- 继续收紧 `RenderWidget3DAdapter` 的边界
- 继续减少 `Main` 对具体渲染实现的直接依赖
- 继续清理文档中的旧目录名和旧实现名

---

## 五、结论

当前 3D 架构已经进入收口阶段。后续所有变更都应围绕“统一主链、减少兼容层、保留必要入口”展开。
