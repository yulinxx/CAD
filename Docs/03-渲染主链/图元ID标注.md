下面给出一个适合当前工程的实现方案。核心原则是：

> **图元 ID 标注不是图元本体几何，不要塞进 `RenderSceneBuilder` 的世界几何中；应该作为独立的 2D 视口标注层，以“世界坐标锚点 + 屏幕像素字号”的方式绘制。**

这样可以保证：

- 不修改图元的几何数据；
- 不影响图元的拾取、排序、LOD 和缓存；
- 缩放时 ID 字体大小保持不变；
- 可以独立显示/隐藏；
- ID 标注只与 2D 视口有关，不污染 Engine 图元接口；
- 可以复用现有 FPS 和文字渲染管线。

---

# 一、先分析当前工程适合复用的部分

当前工程已经具备实现这个功能的大部分基础。

## 1. 现有文字渲染已经支持两种坐标模式

`RenderBridge::TextLayoutInfo` 支持：

```text
Pixel 坐标
World 坐标 + 像素字号
```

其中第二种正好符合本需求：

```text
worldPos = 图元右上角位置
fontSize = 固定像素字号
coordMode = kCoordModeWorldPos
```

现有 `TextQuadBuilder::addText()` 会先把世界坐标转换成屏幕像素坐标，然后使用固定的 `fontSize` 生成字形四边形。

因此：

- 相机缩放只会改变文本的位置；
- 不会改变文本的字号；
- 平移和缩放后，ID 仍然跟随图元；
- 字体大小不会随着世界坐标一起缩放。

这是最适合本需求的实现基础。

## 2. 当前 FPS 也是屏幕空间文字

当前 2D 的 FPS 文字在 `RenderWidget::buildFpsText()` 中通过 `TextQuadBuilder` 提交，并且使用：

```text
coordMode = kCoordModePixel
```

FPS 的字号和位置都是像素单位。

图元 ID 与 FPS 的区别是：

| 内容 | 锚点 | 字号 |
|---|---|---|
| FPS | 固定在视口右下角 | 固定像素 |
| 图元 ID | 跟随图元世界坐标 | 固定像素 |

因此不建议复制 FPS 逻辑，而是复用相同的文字构建器，在其上增加一个独立的“图元标注文本源”。

## 3. 不建议改造 `RenderSceneBuilder`

当前 `RenderSceneBuilder` 负责：

- 图元几何离散化；
- GPU 几何块分配；
- 绘制命令提交；
- 图元几何裁剪；
- 图元 LOD；
- 图元增量更新。

如果把 ID 字符串直接塞进 `RenderSceneBuilder::emitText()`，会产生几个问题：

1. 标注和图元本体绑定太紧；
2. ID 文字的显示开关要影响几何重建；
3. 每次切换设置都可能触发几何重建；
4. 文字会进入图元几何生命周期；
5. 文本的绘制顺序、字体图集和图元本体混在一起；
6. 以后想增加图元名称、类型、锁定状态时会继续污染几何管线。

所以应当保持：

```text
RenderSceneBuilder
    只负责图元本体

EntityIdLabelReconciler
    只负责图元 ID 标注

TextQuadBuilder
    只负责字体排版和 GPU 文字四边形
```

---

# 二、推荐的整体架构

建议新增一个独立组件：

```text
EntityIdLabelReconciler
```

完整数据流如下：

```text
SceneManager
    │
    │ 查询可见图元、图元 ID、图元包围盒
    ▼
EntityIdLabelReconciler
    │
    │ 生成 EntityIdLabelItem
    ▼
RenderWidget
    │
    │ 每帧把 EntityIdLabelItem 转换成 TextLayoutInfo
    ▼
RenderBridge::TextQuadBuilder
    │
    │ WorldPos + PixelSize
    ▼
RenderX
```

建议的数据结构：

```cpp
struct EntityIdLabelItem
{
    uint64_t entityId = 0;

    // 世界坐标中的标注锚点
    Ut::Vec2f anchorWorld;

    // 像素字号
    int fontSize = 12;

    // 屏幕像素偏移，可选
    Ut::Vec2f offsetPx;

    // 颜色
    Ut::Color color;

    // 文本内容，例如 "1024"
    std::string text;
};
```

注意：`EntityIdLabelItem` 不应该直接放入 Engine 图元类中。它属于 UI/Render 层的临时显示数据。

---

# 三、组件职责划分

## 1. `EntityIdLabelReconciler`

建议放在：

```text
Main/Src/UI/Render/EntityIdLabelReconciler.h
Main/Src/UI/Render/EntityIdLabelReconciler.cpp
```

它负责：

- 判断 ID 标注功能是否启用；
- 枚举场景中的图元；
- 判断图元是否可见；
- 判断图层是否可见；
- 判断图元是否位于当前视口或扩展视口；
- 计算图元标注位置；
- 格式化 ID；
- 维护 ID 标注缓存；
- 处理图元新增、删除、修改；
- 将标注数据提交给 `RenderWidget`。

它不应该负责：

- 创建字体；
- 分配 GPU 缓冲；
- 处理 OpenGL；
- 绘制文字；
- 管理相机；
- 修改图元数据。

可以理解为：

```text
它只回答：当前应该显示哪些 ID，以及每个 ID 应该放在哪里。
```

---

## 2. `RenderWidget`

`RenderWidget` 负责：

- 持有当前的 ID 标注数据；
- 每帧通过 `TextQuadBuilder` 绘制；
- 处理字体图集和临时顶点；
- 在设置变化时请求重绘。

建议新增接口：

```cpp
void setEntityIdLabels(std::vector<EntityIdLabelItem> labels);

void clearEntityIdLabels();

void setShowEntityIdLabels(bool visible);

bool showEntityIdLabels() const;
```

或者不让 `RenderWidget` 直接暴露 `std::vector`，采用更低耦合的方式：

```cpp
void setEntityIdLabelSource(const IEntityIdLabelSource* source);
```

不过当前工程已经有 `TextReconciler`、`BitmapReconciler` 这样的账本组件，所以第一阶段建议采用：

```text
EntityIdLabelReconciler 持有数据
RenderWidget 负责消费数据
```

这与现有文字、位图协调方式比较一致。

---

## 3. `TextQuadBuilder`

不需要修改核心渲染逻辑。

只需要将每个 `EntityIdLabelItem` 翻译为：

```cpp
RenderBridge::TextLayoutInfo item;
item.utf8 = label.text.c_str();
item.worldPos = label.anchorWorld;
item.fontSize = label.fontSize;
item.color = label.color;
item.coordMode = RenderBridge::kCoordModeWorldPos;
item.hAlign = RenderBridge::kHAlignLeft;
item.vAlign = RenderBridge::kVAlignTop;
```

关键点是：

```cpp
item.coordMode = RenderBridge::kCoordModeWorldPos;
```

并且：

```cpp
item.fontSize = 固定像素值;
```

不能使用图元世界坐标字号，否则缩放时文字会变大变小。

---

# 四、标注位置设计

## 1. 默认位置：图元包围盒右上方

建议默认位置：

```text
x = entityBounds.maxX + marginWorld
y = entityBounds.maxY + marginWorld
```

其中：

```text
marginWorld = marginPx * pixelToWorld
```

例如：

```text
字体大小：12 px
文字与图元间距：4 px
当前 pixelToWorld：0.2
marginWorld = 4 * 0.2 = 0.8 世界单位
```

这样可以保证标注与图元之间的屏幕间距始终大约是 4 像素。

建议建立统一的布局配置：

```cpp
struct EntityIdLabelStyle
{
    bool visible = false;
    int fontSizePx = 12;
    float marginPx = 4.0f;
    Ut::Color color{1.0f, 0.85f, 0.1f, 1.0f};
    bool drawBackground = false;
    Ut::Color backgroundColor{0.0f, 0.0f, 0.0f, 0.55f};
};
```

第一阶段可以只实现：

```text
visible
fontSizePx
marginPx
color
```

背景框可以放到第二阶段。

---

## 2. 为什么不直接使用固定世界偏移

不建议这样：

```cpp
anchor.x = bounds.maxX + 10.0;
anchor.y = bounds.maxY + 10.0;
```

因为 `10.0` 是世界单位：

- 放大时间距变大；
- 缩小时间距变小；
- 不同单位制下体验不一致。

应该使用：

```cpp
anchor.x = bounds.maxX + marginPx * pixelToWorld;
anchor.y = bounds.maxY + marginPx * pixelToWorld;
```

这会让“图元与 ID 的视觉间距”保持稳定。

---

## 3. 坐标方向需要确认

当前 2D 世界坐标大概率是：

```text
x 向右增加
y 向上增加
```

所以右上方通常是：

```cpp
x = maxX + marginWorld;
y = maxY + marginWorld;
```

但建议不要在 `EntityIdLabelReconciler` 中写死坐标语义，而是统一封装：

```cpp
Ut::Vec2d computeEntityIdAnchor(
    const Ut::BBox2d& bounds,
    double pixelToWorld,
    const EntityIdLabelStyle& style);
```

以后如果需要改成：

- 左上角；
- 右下角；
- 图元中心；
- 跟随鼠标；
- 选中图元显示在右上；
- 文本太靠近边缘时自动换位；

都只改这个布局函数。

---

# 五、如何获得图元包围盒

这是实现中的一个关键问题。

当前 `RenderSceneBuilder` 内部已经计算了每个几何段的 AABB，但这个 AABB 目前主要用于渲染裁剪，并没有作为通用查询接口暴露。

不建议让 `EntityIdLabelReconciler` 读取 `RenderSceneBuilder` 的内部字段。建议有以下两个方案。

## 方案 A：在 UI 层新增图元标注锚点适配器

建议优先采用。

新增一个独立接口：

```cpp
class IEntityLabelGeometryProvider
{
public:
    virtual ~IEntityLabelGeometryProvider() = default;

    virtual bool labelBoundsForEntity(
        Eg::EntityId id,
        Ut::BBox2d& outBounds) const = 0;
};
```

实现类可以放在 2D UI 层：

```text
EntityLabelGeometryProvider2D
```

它负责：

- 根据 `AprEntity` 类型读取几何数据；
- 计算图元的世界 AABB；
- 不参与图元渲染；
- 不修改图元；
- 可单独测试。

优点：

- 不污染渲染构建器；
- 不修改 `ISceneGeometrySink`；
- 不需要把业务布局塞进 Engine；
- 标注系统可以替换自己的定位策略。

缺点：

- 需要为不同图元类型实现包围盒计算。

## 方案 B：复用现有图元几何收集接口

如果当前 Engine 已经有类似：

```cpp
entity->boundingBox()
entity->bounds()
entity->worldBounds()
```

那么直接调用即可。

建议先搜索现有图元和选择系统中是否已经存在统一包围盒接口。如果选择框、缩放选择、`zoomToSelection()` 已经能获得图元范围，应当优先复用那套计算逻辑，而不是重新实现一份。

总体优先级：

```text
已有统一 bounds 接口
    >
已有选择/缩放包围盒服务
    >
新增 EntityLabelGeometryProvider2D
    >
从 RenderSceneBuilder 反向读取内部 AABB
```

最后一种不建议使用。

---

# 六、ID 标注的缓存策略

如果每一帧都遍历所有图元并重新生成文本，会导致大场景性能问题。

建议采用“缓存数据，逐帧绘制”的方式。

## 1. 缓存内容

```cpp
class EntityIdLabelReconciler
{
private:
    struct LabelEntry
    {
        uint64_t id = 0;
        std::string text;
        Ut::Vec2f anchorWorld;
        bool visible = false;
    };

    std::unordered_map<uint64_t, LabelEntry> m_entries;
};
```

注意：

- `text` 可以缓存；
- `anchorWorld` 可以缓存；
- 字号和颜色从样式统一读取；
- 相机变化时不必重新计算世界锚点；
- 只有图元位置、尺寸或显示策略变化时才更新锚点。

## 2. 什么时候重新对账

以下情况触发 `reconcile()`：

### 场景结构变化

- 新增图元；
- 删除图元；
- 打开文件；
- 新建文件；
- 导入文件；
- 撤销/重做导致结构变化。

### 图元几何变化

- 移动；
- 缩放；
- 旋转；
- 编辑控制点；
- 图元尺寸改变。

### 可见性变化

- 图元隐藏/显示；
- 图层隐藏/显示；
- 视口裁剪区域变化；
- 选中时隐藏原图状态变化。

### 设置变化

- 显示/隐藏 ID；
- 字号变化；
- 颜色变化；
- 标注位置策略变化。

### 相机变化

相机变化需要区分两种情况：

#### 只需要重新绘制，不需要重建缓存

- 平移；
- 普通缩放；
- 视口尺寸变化。

因为 `anchorWorld` 仍然有效，`TextQuadBuilder` 每帧会按照最新相机矩阵转换坐标。

#### 需要重新计算标注集合

如果启用了视口裁剪，只显示当前视口附近的 ID，则以下情况需要重新对账：

- 平移后进入新区域；
- 缩放导致新的图元进入视口；
- 视口尺寸变化；
- 视口扩展区域变化。

但是不需要重新计算所有图元的几何，只需要更新可见集合。

---

# 七、视口裁剪策略

如果场景只有几十、几百个图元，可以先简单实现：

```text
所有可见图元都显示 ID
```

如果图元可能达到几万或几十万，不建议一次显示全部 ID。

建议分三阶段。

## 第一阶段：简单可用版

规则：

```text
图元可见
且图层可见
且 ID 显示开关打开
```

全部生成标注。

适合先验证：

- 文字大小是否恒定；
- 锚点是否正确；
- 显示隐藏是否正确；
- 刷新机制是否正确。

## 第二阶段：增加视口裁剪

只显示：

```text
entityBounds.intersects(visibleWorldBounds)
```

其中 `visibleWorldBounds` 可以直接复用当前 2D 视口已有的世界裁剪框。

建议给裁剪范围增加余量：

```text
labelQueryBounds = visibleWorldBounds.expandedBy(margin)
```

这样在边缘平移时不会出现 ID 一闪一闪。

## 第三阶段：增加数量预算

当视口内图元太多时，可以增加限制：

```cpp
size_t maxVisibleLabels = 5000;
```

超过数量时采用策略：

1. 优先显示选中图元；
2. 优先显示鼠标附近图元；
3. 优先显示较大的图元；
4. 按图元层级或文档顺序显示；
5. 其余 ID 暂不显示。

建议第一版先不要加复杂预算，避免需求扩散。

---

# 八、显示/隐藏设置设计

## 1. 设置键

建议使用 2D 专属设置键，而不是放到公共设置中：

```cpp
namespace SettingsKeys
{
    namespace TwoD
    {
        inline constexpr const char* ENTITY_ID_LABELS =
            "2d/entity_id_labels";

        inline constexpr const char* ENTITY_ID_LABEL_FONT_SIZE =
            "2d/entity_id_label_font_size";

        inline constexpr const char* ENTITY_ID_LABEL_COLOR =
            "2d/entity_id_label_color";

        inline constexpr const char* ENTITY_ID_LABEL_MARGIN =
            "2d/entity_id_label_margin_px";
    }
}
```

如果后续 3D 也要支持类似标注，可以再提升到：

```text
common/entity_id_labels
```

但当前需求明确是 2D，所以放在 `2d/` 更符合职责。

## 2. 默认值

建议：

```text
显示：关闭
字号：12 px
边距：4 px
颜色：黄色或橙黄色
```

理由：

- ID 属于诊断/调试辅助信息；
- 默认打开可能污染正常绘图界面；
- 与当前 FPS 设置类似，属于显示辅助功能；
- 用户明确打开后才显示。

## 3. 不要把设置直接写进 `UiStateCenter`

`UiStateCenter` 主要是运行时 UI 状态中心，不适合承载所有持久化配置。

建议区分：

```text
SettingsService / SettingsPersistenceHelper
    持久化设置值

SettingsUiCoordinator2D
    读取设置、响应控件

RenderViewport2D / EntityIdLabelReconciler
    使用运行时设置

UiStateCenter
    不保存该设置
```

这样可以避免：

- 关闭应用后设置丢失；
- UI 状态中心越来越臃肿；
- 2D/3D 设置和视图运行时状态混在一起。

---

# 九、设置变更后的调用链

设置面板中勾选“显示图元 ID”之后，建议走以下流程：

```text
设置复选框改变
    ▼
SettingsUiCoordinator2D
    ▼
SettingsService 写入持久化存储
    ▼
RenderViewport2D::setShowEntityIdLabels(bool)
    ▼
EntityIdLabelReconciler::setEnabled(bool)
    ▼
如果 false：
    清空 RenderWidget 的 ID 文本缓存
如果 true：
    重新对账当前可见图元
    生成 ID 标注缓存
    请求 repaint
```

伪代码：

```cpp
void RenderViewport2D::setShowEntityIdLabels(bool visible)
{
    if (!m_renderCoordinator)
    {
        return;
    }

    m_renderCoordinator->setShowEntityIdLabels(visible);
    requestRepaint();
}
```

协调器内部：

```cpp
void EntityIdLabelReconciler::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
    {
        return;
    }

    m_enabled = enabled;

    if (!m_enabled)
    {
        m_labels.clear();
        m_renderWidget->clearEntityIdLabels();
        return;
    }

    reconcileFull();
}
```

---

# 十、建议的类关系

推荐结构：

```text
RenderViewport2D
    ├── RenderWidget
    ├── SceneRefreshCoordinator
    └── EntityIdLabelReconciler
            ├── SceneManager
            ├── IEntityLabelGeometryProvider
            └── EntityIdLabelStyle
```

其中：

```text
RenderViewport2D
```

只负责生命周期和相机变化通知。

```text
SceneRefreshCoordinator
```

负责在场景变更时通知标注协调器。

```text
EntityIdLabelReconciler
```

负责场景到标注数据的转换。

```text
RenderWidget
```

负责把标注数据送入文字绘制管线。

不要让以下模块直接互相依赖：

```text
EntityIdLabelReconciler → RenderSceneBuilder
EntityIdLabelReconciler → GeometryManager
EntityIdLabelReconciler → RenderX
```

标注层只需要依赖：

```text
SceneManager
RenderWidget 的文字提交接口
RenderBridge::TextLayoutInfo
```

---

# 十一、RenderWidget 中的绘制位置

当前 `RenderWidget` 已经有：

```text
buildSceneEnvText()
submitRulerOverlay()
buildFpsText()
```

建议新增一个独立函数：

```cpp
void RenderWidget::buildEntityIdLabels(
    const std::vector<EntityIdLabelItem>& labels,
    uint32_t vpWidth,
    uint32_t vpHeight);
```

执行逻辑：

```text
1. 如果 ID 标注为空，直接返回
2. m_text.beginFrame()
3. 获取当前 camera view matrix
4. 遍历 labels
5. 每个 label 转换为 TextLayoutInfo
6. coordMode = WorldPos
7. fontSize 使用固定像素值
8. 调用 m_text.addText()
9. flush()
```

建议使用独立的渲染层：

```cpp
constexpr uint8_t kEntityIdLabelLayer = 210;
```

当前图层大致可以保持：

```text
100：图元本体
200：选择/交互覆盖层
210：图元 ID 标注
220：FPS / HUD
```

具体层号不重要，关键是明确规则：

- ID 标注不被图元本体覆盖；
- ID 标注不影响选择框；
- ID 标注不影响 FPS；
- ID 标注在选中轮廓上方或下方要有明确设计。

我建议：

```text
图元本体 < 选择轮廓 < ID 标注 < FPS/HUD
```

如果 ID 太多会遮挡选择框，也可以将 ID 标注放在选择轮廓下面。

---

# 十二、增量更新建议

不要每次图元修改都全量生成所有标签。

可以让 `EntityIdLabelReconciler` 支持：

```cpp
void rebuildAll();

void updateEntities(const std::vector<Eg::EntityId>& ids);

void removeEntities(const std::vector<Eg::EntityId>& ids);

void refreshViewport(const Ut::BBox2d& visibleBounds);
```

## 新增图元

```text
SceneRefreshCoordinator 收到新增 ID
    ▼
EntityIdLabelReconciler::updateEntities({id})
```

## 删除图元

```text
SceneRefreshCoordinator 收到删除 ID
    ▼
EntityIdLabelReconciler::removeEntities({id})
```

## 图元移动或改变大小

```text
图元 dirty
    ▼
重新计算该图元的 bounds
    ▼
更新对应 LabelEntry::anchorWorld
```

## 相机移动

```text
相机变化
    ▼
更新当前 visibleWorldBounds
    ▼
只重新对账可见集合
```

## 显示开关变化

```text
关闭：clear labels
打开：rebuildAll 或 refreshViewport
```

---

# 十三、图层、图元可见性和选中状态

标注的可见条件应当与图元本体保持一致。

建议统一判断：

```cpp
bool shouldShowLabel(const Eg::AprEntity& entity) const
{
    if (!entity.visible())
    {
        return false;
    }

    if (entity.layer() && !entity.layer()->isVisible())
    {
        return false;
    }

    if (!isInVisibleWorldBounds(entity))
    {
        return false;
    }

    return true;
}
```

如果当前启用了：

```text
选中时隐藏原图
```

建议仍然显示 ID 标注，除非产品需求明确要求选中时也隐藏。

比较合理的规则是：

| 状态 | 本体 | ID |
|---|---|---|
| 普通可见 | 显示 | 显示 |
| 图元隐藏 | 隐藏 | 隐藏 |
| 图层隐藏 | 隐藏 | 隐藏 |
| 选中且隐藏本体 | 隐藏 | 可显示 |
| 图元删除 | 删除 | 删除 |
| ID 标注开关关闭 | 显示 | 隐藏 |

这样在编辑、调试时仍然能看到被选中的图元 ID。

---

# 十四、ID 文本格式

第一版建议直接使用十进制字符串：

```cpp
std::to_string(entity.id)
```

对于当前 `EntityId` 可能出现的特殊值，需要统一规则。

建议：

```text
持久图元 ID：显示十进制正数
临时图元 ID：显示为 T-1、T-2 或者直接隐藏
0：不显示
INT64_MAX 附近的内部合成 ID：绝不显示
```

当前 `RenderSceneBuilder` 中存在合成 owner ID，用于无 ID 几何段的内部归属。这些 ID 不应该作为用户看到的图元 ID。

因此标注数据源必须来自：

```text
SceneManager 中真实的 AprEntity ID
```

不能从：

```text
RenderSceneBuilder 的 m_pieces
DrawInstruction::userData
```

反推 ID。

建议集中封装：

```cpp
std::string formatEntityId(Eg::EntityId id);
```

这样以后可以切换成：

```text
ID: 1024
E1024
#1024
实体 1024
```

而不改渲染逻辑。

---

# 十五、背景框建议放在第二阶段

如果只绘制文字，复杂场景中可能出现：

- ID 与图元颜色接近；
- ID 压在填充区域上不清晰；
- 多个 ID 重叠；
- 背景色复杂导致难以识别。

第二阶段可以增加背景框，但不要一开始就修改字体管线。

可以先做一个简单的屏幕空间背景矩形：

```text
ID 文本四边形
    ▼
根据字体测量结果得到像素宽高
    ▼
在文字前后增加一个 Screen 空间矩形
```

或者把 ID 标注扩展成：

```cpp
struct EntityIdLabelItem
{
    ...
    bool drawBackground = true;
    Ut::Color backgroundColor;
    float paddingPx = 2.0f;
};
```

但第一版建议只做纯文字，先把数据流和刷新机制跑通。

---

# 十六、推荐的具体实施步骤

## 第 1 步：确认现有包围盒接口

先搜索当前工程中：

```text
boundingBox
bounds
worldBounds
selection bounds
zoomToSelection
```

重点检查：

- `AprEntity` 是否已有 bounds；
- 选择服务是否已经计算过 AABB；
- `ViewportSelector` 是否有统一的几何范围计算；
- `SceneManager` 是否提供空间查询。

如果已有统一接口，直接复用。

如果没有，再新增：

```text
EntityLabelGeometryProvider2D
```

不要先改渲染构建器。

---

## 第 2 步：新增中性标注数据结构

建议新增文件：

```text
Main/Src/UI/Render/EntityIdLabelTypes.h
```

内容职责：

```text
EntityIdLabelItem
EntityIdLabelStyle
EntityIdLabelPlacement
```

不要在这个头文件里包含：

- `RenderWidget.h`
- `QOpenGLWidget`
- `Renderx.h`
- Engine 复杂实现头

尽量保持为纯数据结构。

---

## 第 3 步：新增 `EntityIdLabelReconciler`

文件：

```text
Main/Src/UI/Render/EntityIdLabelReconciler.h
Main/Src/UI/Render/EntityIdLabelReconciler.cpp
```

先实现以下接口：

```cpp
class EntityIdLabelReconciler
{
public:
    explicit EntityIdLabelReconciler(RenderWidget* widget);

    void setSceneManager(Eg::SceneManager* sceneManager);
    void setGeometryProvider(IEntityLabelGeometryProvider* provider);

    void setStyle(const EntityIdLabelStyle& style);
    void setEnabled(bool enabled);

    void reconcileFull(const Ut::BBox2d& visibleWorldBounds);
    void updateEntities(const std::vector<Eg::EntityId>& ids);
    void removeEntities(const std::vector<Eg::EntityId>& ids);
    void clear();

    const std::vector<EntityIdLabelItem>& labels() const;
};
```

第一版可以只实现 `reconcileFull()`，增量接口随后补齐。

---

## 第 4 步：在 `RenderWidget` 增加标签缓存和绘制接口

新增：

```cpp
void setEntityIdLabels(std::vector<EntityIdLabelItem> labels);
void clearEntityIdLabels();
```

新增内部成员：

```cpp
std::vector<EntityIdLabelItem> m_entityIdLabels;
```

新增：

```cpp
void buildEntityIdLabels(uint32_t vpWidth, uint32_t vpHeight);
```

在每帧提交流程中，放在场景几何之后、FPS 之前：

```text
提交场景几何
提交普通世界文字
提交选择覆盖层
提交图元 ID
提交 FPS
```

如果希望 ID 在选择框下方，则调整到覆盖层之前。

---

## 第 5 步：实现世界坐标 + 固定像素字号

对每个 ID：

```cpp
RenderBridge::TextLayoutInfo item;
item.utf8 = label.text.c_str();
item.worldPos = label.anchorWorld;
item.fontSize = style.fontSizePx;
item.coordMode = RenderBridge::kCoordModeWorldPos;
item.hAlign = RenderBridge::kHAlignLeft;
item.vAlign = RenderBridge::kVAlignTop;
item.color = style.color;
```

不要把 `fontSizePx` 乘以 zoom。

不要把 `fontSizePx` 转换为世界单位。

这是保证“不随缩放变化”的关键。

---

## 第 6 步：接入 `SceneRefreshCoordinator`

在当前刷新协调器中增加一个标注刷新分支。

建议逻辑：

```text
full refresh
    → EntityIdLabelReconciler::reconcileFull()

incremental refresh
    → EntityIdLabelReconciler::updateEntities(dirtyIds)
    → EntityIdLabelReconciler::removeEntities(removedIds)

camera changed
    → 仅当启用视口裁剪时刷新可见集合
    → 始终 requestRepaint()
```

注意：

```text
相机缩放本身不需要重建文本内容。
```

只要文本使用 `WorldPos + PixelSize`，每帧的相机矩阵会自动处理位置变化。

---

## 第 7 步：接入设置系统

新增 2D 设置键和默认值。

设置 UI 增加：

```text
显示图元 ID
```

初期只放一个复选框。

设置改变时：

```text
SettingsUiCoordinator2D
    → RenderViewport2D::setShowEntityIdLabels()
    → EntityIdLabelReconciler::setEnabled()
    → requestRepaint()
```

建议不要把这个设置和 FPS 设置复用同一个键。

---

## 第 8 步：增加测试

建议至少增加以下测试。

### 1. 固定字号测试

验证：

```text
zoom = 0.5
zoom = 1.0
zoom = 10.0
fontSize 始终为 12
```

测试对象应是 `TextLayoutInfo` 或标注布局数据，不要依赖真实窗口。

### 2. 锚点计算测试

输入：

```text
bounds = [10, 20, 100, 200]
marginPx = 4
pixelToWorld = 0.5
```

期望：

```text
anchor.x = 102
anchor.y = 202
```

### 3. 可见性测试

验证：

- 图元隐藏时无标签；
- 图层隐藏时无标签；
- 图元显示时有标签；
- 删除图元后标签被移除。

### 4. 开关测试

验证：

```text
关闭 → 标签为空
打开 → 当前可见图元出现标签
```

### 5. 增量更新测试

验证：

```text
图元位置改变 → 只有该图元锚点更新
图元删除 → 只有该图元标签移除
未修改图元 → 标签不重建
```

### 6. 特殊 ID 测试

验证：

```text
id = 0 不显示
持久 ID 正常显示
临时 ID 按规则处理
内部合成 ID 不会进入标签
```

---

# 十七、建议的验收标准

功能完成后，可以按以下标准验收。

## 显示行为

- 开启设置后，2D 视图中的可见图元出现 ID；
- 关闭设置后，所有 ID 消失；
- 图层隐藏后，对应 ID 消失；
- 图元隐藏后，对应 ID 消失；
- 删除图元后，ID 不残留；
- 打开、新建、导入、撤销、重做后显示正确。

## 缩放行为

- 放大时 ID 字体大小不变；
- 缩小时 ID 字体大小不变；
- 平移时 ID 跟随图元；
- 视口边缘的 ID 不发生明显闪烁；
- 文字不因相机缩放变模糊或变大。

## 性能行为

- 缩放时不触发所有图元几何重建；
- 显示开关只刷新标注层，不重建图元本体；
- 修改一个图元时，不重新计算所有 ID；
- 大场景不会每帧遍历完整场景；
- ID 标签不影响图元几何合批和 LOD。

## 架构行为

- `RenderSceneBuilder` 不增加 ID 文本业务逻辑；
- Engine 图元类不增加 UI 显示字段；
- `TextQuadBuilder` 不依赖 `SceneManager`；
- 设置系统不依赖 OpenGL；
- 标注组件可以独立进行单元测试。

---

# 十八、最终推荐的最小实现版本

建议第一版严格控制范围，只实现：

```text
1. 新增 EntityIdLabelReconciler
2. 查询可见图元
3. 计算 AABB 右上角
4. 生成十进制 ID
5. 通过 TextQuadBuilder 绘制
6. World 坐标锚点
7. 固定像素字号
8. 一个显示/隐藏设置
9. 场景全量刷新时重建
10. 相机变化时只 requestRepaint
```

第一版暂时不要加入：

```text
1. 背景框
2. 自动避让
3. 标签碰撞检测
4. 最大标签数量
5. 多种 ID 格式
6. 旋转文本
7. 选中状态特殊样式
8. 标签拖动
9. 文本缓存纹理优化
```

推荐最终的职责边界如下：

```text
SceneManager
    提供图元和 ID

EntityLabelGeometryProvider2D
    提供图元标注位置

EntityIdLabelReconciler
    维护哪些标签应该显示

RenderWidget
    每帧提交标签

TextQuadBuilder
    完成字体排版和绘制

SettingsService
    持久化显示开关和样式

RenderSceneBuilder
    完全不参与 ID 标签
```

这个方案和当前工程已经存在的 `TextReconciler`、`TextQuadBuilder`、`RenderWidget`、`SceneRefreshCoordinator` 的结构是兼容的，后续你按照这个方向编写代码时，可以先从“全量刷新版”开始，再逐步增加增量更新和视口裁剪。