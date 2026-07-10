# OperationBus 优先的变换命令稳定化计划

## 背景

当前项目的变换能力已经从单一命令分发，演进为以 `OperationBus` 为主线的操作系统：

- `OperationBus` 负责操作调度与执行入口
- `TransformDialogAdapter` 负责精确参数输入
- `MouseInteractionAdapter` 负责鼠标交互式参数收集
- `TransformParameters` 负责强类型参数传递
- `SceneEditServiceAdapter` 负责文档执行、预览、提交与取消
- `UiEntity` / `EntityDocument2D` 负责实体数据存储与修改
- 旧 `UiCommandDispatcher` 仅作为兼容层保留

本计划目标是：先在现有框架上稳定 `Move / Rotate / Mirror` 三条核心变换链，确保“对话框输入”和“鼠标交互”两条路径得到一致结果；再扩展到 `Copy / Trim / Extend`；最后继续收口旧桥，并让渲染刷新链路更加稳定。

---

## 当前工作原则

- 以现有框架为基础，不再大规模搭新框架
- 先验收 `Move / Rotate / Mirror` 的结果一致性，再做功能复制扩展
- 输入层只负责收集参数，不负责执行和调度
- 文档执行层只负责实体变换，不负责 UI 和命令调度
- 预览、提交、取消必须走同一条参数模型
- 渲染刷新由文档变更触发，不由旧 Dispatcher 兜底
- 旧 Dispatcher 仅保留历史兼容与过渡期接入

---

## 当前稳定结构

```text
Input Providers
├── TransformDialogAdapter
└── MouseInteractionAdapter
        ↓
TransformParameters
        ↓
OperationBus::run(OperationId, TransformParameters)
        ↓
IOperation::execute(ctx)
        ↓
SceneEditServiceAdapter
        ↓
EntityDocument2D / UiEntity
        ↓
Render refresh callback / viewport update
```

---

## 第一部分：现有框架内的检查顺序

这一部分不是搭框架，而是验收当前链路是否足够稳定。

### 1.1 先检查 Move / Rotate / Mirror 三条路径的结果一致性

#### 要确认的内容
- 对话框输入是否稳定
- 鼠标交互是否稳定
- 预览是否稳定
- `Enter / Esc` 是否稳定
- 提交后渲染是否刷新
- 取消后是否完全回滚

#### 检查方式
1. 对同一组实体，分别走对话框路径和鼠标路径
2. 记录最终变换结果
3. 对比几何结果、selection 结果、渲染结果
4. 对比确认与取消后的最终状态

#### 通过标准
- 同一操作在两条路径下得到一致结果
- 预览不漂移
- 取消可回滚
- 提交后刷新正确

---

### 1.2 再检查参数语义是否统一

#### 要确认的内容
- `Move`：`moveX / moveY`
- `Copy`：`copyCount / moveX / moveY`
- `Rotate`：`rotateAngle / anchorX / anchorY`
- `Mirror`：`mirrorAxis` + 轴参数或镜像线参数

#### 检查方式
1. 检查对话框输入是否写入同一组字段
2. 检查鼠标输入是否映射成同一组字段
3. 检查 `IOperation` 是否只读取统一参数结构

#### 通过标准
- 不再在执行层以字符串 key 作为主路径
- `TransformParameters` 是唯一主参数来源
- 旧 `QVariantMap` 仅保留兼容 fallback

---

### 1.3 再检查 `SceneEditServiceAdapter` 行为一致性

#### 要确认的内容
- `previewTransform()` 只负责预览，不提交
- `commitTransform()` 只负责正式落库
- `cancelTransform()` 必须完整回滚到原始状态
- 通过回调通知视图刷新，而不是直接依赖具体视图类

#### 检查方式
1. 执行 Move/Rotate/Mirror 的预览
2. 检查是否只改变临时状态
3. 执行提交后检查是否真正写回文档
4. 执行取消后检查是否精确恢复

#### 通过标准
- 原始状态缓存完整
- 取消不会留下残留状态
- 提交后渲染链可刷新

---

## 第二部分：在现有框架上完成的功能顺序

这一部分是“收口 + 验证 + 复制扩展”的实施顺序。

### 2.1 第一阶段：把 Move / Rotate / Mirror 全部验稳

#### 目标
先把当前三条变换链跑稳，不再扩大范围。

#### 需要确认的点
- 对话框路径稳定
- 鼠标交互路径稳定
- 预览稳定
- `Enter / Esc` 稳定
- 提交后刷新稳定
- 取消后回滚稳定

#### 结果标准
- Move / Rotate / Mirror 三条路径结果一致
- 对话框与鼠标两种输入方式一致
- 预览和提交语义一致

#### 分项检查

##### Move
- 对话框路径输出 `moveX / moveY`
- 鼠标路径输出同一组位移参数
- 选中实体是否正确移动
- `Enter` 是否确认，`Esc` 是否取消
- 提交后是否刷新
- 取消后是否回到原位

##### Rotate
- 对话框路径输出 `rotateAngle / anchorX / anchorY`
- 鼠标路径输出同一组旋转参数
- 锚点和角度是否一致
- 提交后旋转结果是否一致
- 取消后是否回滚

##### Mirror
- X 轴、Y 轴、任意线三种模式是否可区分
- 对话框路径和鼠标路径是否输出同一语义
- 预览线是否正确
- 提交后镜像结果是否一致
- 取消后是否回滚

---

### 2.2 第二阶段：把同样模式复制到 Copy / Trim / Extend

#### 目标
在验稳 Move / Rotate / Mirror 后，复制既有样板，不再重造框架。

#### 推荐顺序
1. `Copy`
2. `Trim`
3. `Extend`

#### 复制原则
- 复用 `TransformParameters`
- 复用 `TransformDialogAdapter`
- 复用 `MouseInteractionAdapter`
- 复用 `SceneEditServiceAdapter`
- 复用预览 / 提交 / 取消闭环

#### 结果标准
- 新增操作不再引入新的交互框架
- 每个操作只补业务语义，不补新的架构层

---

### 2.3 第三阶段：同步开始做渲染主链

#### 目标
在实体可修改后，把“实体数据如何进入渲染器”打通。

#### 主链
- 文档
- selection
- 渲染模型
- renderer

#### 你要做的完整流程

##### 2.3.1 文档层发出“已变更”信号

目标是让所有实体修改都能通知视图，而不是由某个操作类直接刷新 UI。

需要确认：
- 变更来自 `commitTransform()` 后能通知到视图
- 选择变化也能触发通知
- 预览和取消不污染正式变更通知

建议拆成两个语义：
- **场景已变更**：实体数据真的改了
- **选择已变更**：高亮状态或 selection 发生变化

这样后面调试时很清楚是“数据变了”还是“只选中态变了”。

---

##### 2.3.2 统一视图刷新入口

目标是把刷新路径收拢成少数几个明确入口，避免散落在各个 operation 里。

建议的刷新入口职责：
- 重新同步 selection
- 更新 render data
- 触发 viewport repaint
- 更新属性/状态面板（如已有）

需要确认：
- 刷新不能依赖 operation 自己手动调用多个 UI 函数
- 刷新不能藏在临时预览状态里
- 刷新必须能区分“预览刷新”和“提交刷新”

建议最终保留这类统一动作：
- `syncSelectionFromScene()`
- `updateRenderData()`
- `viewport->update()` 或等价入口

---

##### 2.3.3 建立文档 → 视图的刷新链

目标是明确数据如何从文档流到屏幕。

推荐链路：
1. `SceneEditServiceAdapter` 提交/取消
2. 文档状态变化
3. 文档通知刷新回调
4. `ViewWidgetAdapter` 同步 selection 与 render data
5. `Viewport2D` 重新绘制

这里要避免的情况：
- operation 直接操作视图
- 文档层知道具体 QWidget 实现
- 刷新回调里又反过来改文档，形成循环

---

##### 2.3.4 建立 selection 高亮链

目标是 selection 的显示和文档中的 selection 事实一致。

需要做的事：
- selection 改变后立即刷新高亮
- 高亮数据来源统一来自场景/文档
- 不能在 viewport 里维护一份独立 selection 真相

需要确认的点：
- 选中实体与高亮实体完全一致
- 执行 Move/Rotate/Mirror/Copy/Trim/Extend 后，selection 不丢、不乱
- 取消时 selection 不被错误清空或错误保留

---

##### 2.3.5 建立最小渲染闭环

目标不是一次性做完整渲染系统，而是先让“改文档 → 刷新 → 看见变化”跑通。

最小闭环应至少覆盖：
- 一条线的位移
- 一次旋转
- 一次镜像
- selection 高亮变化

建议优先顺序：
1. 先让已有图元能在刷新后正确重绘
2. 再让 selection 高亮跟上
3. 再考虑更复杂图元或局部刷新优化

---

#### 结果标准
- 文档变更后视图自动刷新
- selection 改变后高亮同步更新
- 渲染链和操作链分离清楚
- 预览/提交/取消的刷新时机正确

---

### 2.4 第四阶段：再回头清旧桥

#### 目标
在新链路稳定后，再逐步收口旧实现。

#### 清理对象
- 旧 dispatcher
- 旧 `enter*Mode()`
- 旧 `ToolContext`

#### 原则
- 不急着删
- 不再新增旧路径
- 只保留过渡兼容

---

## 第三部分：当前不建议做的事

- 不要再大改 `OperationBus`
- 不要再继续扩输入层结构
- 不要再写新的交互抽象
- 不要急着删旧 dispatcher
- 不要把 `Viewport2D` 再加厚

---

## 第四部分：按文件参考的修改清单

### 4.1 输入层

#### `TransformDialogAdapter`
负责对话框参数输入。

#### `MouseInteractionAdapter`
负责鼠标交互输入。

#### `TransformParameters`
负责统一参数语义。

**修改目标**
- 保证两种输入方式产出同一参数结构
- 保证语义一致
- 保证确认 / 取消一致

---

### 4.2 操作层

#### `MoveOperation`
#### `RotateOperation`
#### `MirrorOperation`
#### `CopyOperation`

**修改目标**
- 只读取 `TransformParameters`
- 只操作选中实体
- 统一预览 / 提交 / 取消流程

---

### 4.3 文档执行层

#### `SceneEditServiceAdapter`
#### `EntityDocument2D` / `UiEntity`

**修改目标**
- 负责实体级变换执行
- 负责预览回滚
- 负责提交后状态同步

---

### 4.4 刷新链路

#### `ViewWidgetAdapter`
#### `Viewport2D`

**修改目标**
- 提交后刷新
- 取消后回滚
- selection 变化后高亮同步
- 建立最小渲染闭环

---

### 4.5 旧桥收口准备

#### `UiCommandDispatcher`
#### `UiCommandHandler`
#### `Viewport2D::enter*Mode()`

**修改目标**
- 停止新增旧路径
- 仅保留兼容
- 等新链路稳定后再逐步收口

---

## 第五部分：验证方案

### Move 验证
1. 选中实体
2. 对话框输入 `moveX / moveY`
3. 鼠标拖拽输入同样位移
4. 比较结果一致
5. 提交后视图刷新
6. 取消后实体回到原位

### Rotate 验证
1. 对话框输入角度和中心点
2. 鼠标交互输入旋转结果
3. 比较最终结果一致

### Mirror 验证
1. X 轴镜像
2. Y 轴镜像
3. 任意线镜像
4. 三种模式分别预览、提交、取消

### 渲染验证
1. 任意命令提交后视图自动刷新
2. selection 改变后高亮同步更新
3. 取消不会污染状态
4. 至少一个基础图元能在刷新后可见变化

---

## 第六部分：本计划不做的事

- 不再把新能力继续塞回旧 `ICommandHandler` 主线
- 不再让输入层承担执行职责
- 不再让文档执行层依赖具体视图类
- 不再把刷新逻辑散落在多个命令里

---

## 结论

本计划的核心是：

- 先在现有框架上验收 Move / Rotate / Mirror 的结果一致性
- 再复制扩展到 Copy / Trim / Extend
- 然后同步打通渲染主链
- 最后再逐步收口旧 Dispatcher 与旧工具桥

如果后续要继续扩展，最稳妥的方式是：
**先稳定 Move / Rotate / Mirror，再复制到 Copy / Trim / Extend，最后收口旧栈。**
