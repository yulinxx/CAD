# macOS 渲染问题笔记

> 记录 macOS 平台 2D/3D 渲染相关问题的根因、修复方案与平台差异。Windows/Linux 如遇类似症状可参考对照。
> 最后更新：2026-08-14

---

## 1. 平台 GL 版本策略

| 平台 | OpenGL 版本 | Profile | 说明 |
|------|------------|---------|------|
| Windows / Linux | 4.6 | CoreProfile | `main.cpp` 全局默认格式 |
| macOS | 4.1（系统最高） | CoreProfile | macOS 只支持到 4.1 |

- 全局默认格式在 `Main/Src/main.cpp` 的 `QSurfaceFormat::setDefaultFormat()` 设置，**必须在 QApplication 创建之前**。
- 项目已全部迁移到现代 OpenGL（shader + VBO/VAO），无固定管线（glBegin/glMatrixMode/glLight* 等）。
- `QOpenGLFunctions_3_3_Core` 只是 Qt 的函数指针封装类（最高 3.3），**不代表上下文版本**；上下文版本由 QSurfaceFormat 决定。3.3 core 子集在 4.6/4.1 上完全可用。

---

## 2. 重复台面/网格（左上/右上角出现复制品）+ 网格不均匀

### 症状
- 2D 视口左上角或右上角出现"多余的台面+网格"复制品，位置随窗口布局/尺寸变化。
- 网格背景线粗细/间距不均匀。

### 根因（RHI GL 后端非 DSA 路径忽略 per-layer 顶点偏移）
`Renderx/src/rhi/rhi_gl.cpp` 的 `GLDevice::bindVertexBuffer()`：

- DSA 路径（`glVertexArrayVertexBuffer`，OpenGL 4.3+）：偏移作为参数传给驱动，**正确**。
- 非 DSA fallback 路径（macOS 4.1 没有该函数）：`configureVertexAttribs()` 里的
  `glVertexAttribPointer` 偏移**写死 0/12**，`bindVertexBuffer` 传入的图层字节偏移被丢弃。

后果：SceneEnv 的共享顶点缓冲中，图层 1-9（边框/网格/标尺）全部从**顶点 0（台面表面）**开始读数据：

1. 标尺背景层（三角形 + 像素坐标 + 像素矩阵）拿台面的 6 个顶点以像素矩阵又画了一遍
   → 就是"左上/右上多余的台面+网格复制品"（像素矩阵与视口尺寸相关，所以位置随窗口布局变化）。
2. 边框/网格图层也读错顶点 → 线条出现在错误位置 → "网格不均匀"。

Windows/Linux 走 DSA 路径（4.6 支持），所以只在 Mac 出现。

### 修复
`configureVertexAttribs()` 增加 `baseOffset` 参数，fallback 路径的
`glVertexAttribPointer` 偏移 = 属性内偏移 + 图层字节偏移。所有顶点格式
（P3C3/P3C4/P3N3/P3T2/P3T2C4/P2T2C4）的 fallback 分支都已处理。

```cpp
// rhi_gl.cpp
void GLDevice::configureVertexAttribs(GLFuncs* g, PrimitiveTopology, VertexFormat fmt, uint64_t baseOffset);
// fallback: (void*)(uintptr_t)(baseOffset + 12) 等
// bindVertexBuffer 调用点传入 offset
```

**影响面**：该 bug 影响所有在非 DSA 上下文里用非零偏移调 `bindVertexBuffer` 的绘制路径
（SceneEnv、BatchQueue、OverlayQueue、ScreenText 等），Mac 上全部修复。
Windows 的 DSA 路径本就正确，无行为变化。

---

## 3. macOS 必须构建 .app Bundle（NSHighResolutionCapable）

### 症状
- 裸可执行文件（无 .app、无 Info.plist）运行时，macOS 以 1x 低分辨率合成窗口，
  而 Qt6 按 Retina（dpr=2）渲染 → GL 内容按错误比例合成、残留在视图角落，
  表现为重复幅面/错位、网格不均匀。位置随窗口布局变化。
- 用 `open` 启动或直接运行 bundle 内二进制均可，关键是**进程必须处于 bundle 中**。

### 修复
- `Main/Info.plist.in`：包含 `NSHighResolutionCapable = true`（关键键）。
- `Main/CMakeLists.txt`：`MACOSX_BUNDLE TRUE` + `MACOSX_BUNDLE_INFO_PLIST`，
  配置期用 `string(CONFIGURE @ONLY)` 生成（注意 CMake 变量大小写：模板里要用 `@app_name@`）。
- 运行方式：`open SanYiCAD.app` 或 `SanYiCAD.app/Contents/MacOS/SanYiCAD`。

---

## 4. 全黑视口：Renderx 着色器/字体文件缺失

### 症状
bundle 化后视口全黑。日志出现：
```
[Shader] Failed to open shader file: .../Contents/MacOS/scene_2d.vert
renderCreateDevice: default font not found: .../default_screen_font.ttf
[CommandEncoder] failed to create overlay line pipeline
RenderWidget::initializeGL: failed to create render device
```

### 根因
Renderx 运行时从**应用可执行文件所在目录**（`_NSGetExecutablePath` 的 parent）加载
`.vert/.frag/.comp` 着色器和 `default_screen_font.ttf`。
Renderx 自身的 POST_BUILD 只复制到**其 dylib 输出目录**（`bin_Qt6/Debug`），
bundle 化后应用二进制移到 `Contents/MacOS`，文件不在那里。

### 修复
`Main/CMakeLists.txt` 为 app 目标添加 POST_BUILD：
把 `${SANYI_RENDERX_DIR}/src/shader/*.{vert,frag,comp}` 与
`src/res/*.ttf` 复制到 `$<TARGET_FILE_DIR:${app_name}>`。

**注意**：着色器源文件（.vert/.frag）修改后**不会**触发 CMake 重新构建/重新复制
（不是构建依赖），调试 shader 时要手动复制到 bundle，否则跑的是旧 shader。

---

## 5. QSurfaceFormat 不一致 + PartialUpdate 重影

### 症状
widget 单独请求与全局默认不同的格式（如 4.6 + 4x MSAA vs 全局 4.1）时，
Qt 无法创建共享 NSOpenGLContext，渲染内容错位/残留（左上角重复内容）。

### 修复
- macOS：`RenderWidget`（2D）构造里**不调用 setFormat**，直接沿用 `main.cpp` 全局默认
  （4.1 CoreProfile、samples=0）。
- macOS 上 `QOpenGLWidget::PartialUpdate` 合成不可靠（配合全屏重绘产生重影），
  Mac 分支用 `setUpdateBehavior(QOpenGLWidget::NoPartialUpdate)`；
  Windows 分支保持 4.6 CoreProfile + PartialUpdate。

---

## 6. 3D 视图 RenderWidget3D 现代 OpenGL 迁移

- `RenderWidget3D` 已从固定管线（glBegin/glMatrixMode/glLight*）改写为
  shader + `QOpenGLVertexArrayObject`/`QOpenGLBuffer`：
  - `initPrograms()`：线 shader（pos+color）、mesh shader（pos+normal + Phong 光照）。
  - 网格/坐标轴/台面用线程序 + VAO/VBO 绘制；选中图元高亮填充 + 黄色线框叠加。
- `m_glFunctions` 类型为 `QOpenGLFunctions_3_3_Core*`（有 `glPolygonMode`/`glLineWidth`；
  基础 `QOpenGLFunctions` 没有）。
- Qt 6.11 无 `QOpenGLContext::versionFunctions<T>()`，用
  `new QOpenGLFunctions_3_3_Core(); initializeOpenGLFunctions();` 获取。
- 着色器用 `#version 330 core`（4.6/4.1 都支持）。
- 日志宏：`SY_ERROR` 是单参数宏，带格式用 `SY_ERRORF`。

---

## 7. 调试方法论（像素级取证）

模型无法读取截图时，用帧缓冲导出 + 像素分析定位渲染问题：

1. 在 `paintGL` 里 `glReadPixels` 导出 RGBA 到文件（注意 GL 行序：row 0 = 底部）。
2. 用 Python 脚本分析：颜色直方图、目标区域边界、逐行剖面。
3. 验证链：顶点数据（CPU 日志）→ GL 属性绑定（VAO 状态）→ uniform（GL readback）→
   shader（把矩阵编码成颜色输出）→ 图层归属（每层上不同纯色）。
4. 快捷实验：临时改 shader 输出 `gl_Position = aPosition`（绕过矩阵）可区分
   "顶点数据错"还是"矩阵错"。

---

## 8. 跨平台兼容性（Windows/Linux）

- **GL 上下文**：Windows/Linux 用 4.6 CoreProfile（全局默认），macOS 4.1。
- **RHI 偏移修复**：DSA 路径（4.6）不受影响，fallback 修复只改变非 DSA 行为 → Windows 行为不变。
- **bundle 相关 CMake**：`MACOSX_BUNDLE_*` 属性在 Windows/Linux 被忽略；plist 生成有 `if(APPLE)` 保护。
- **shader/字体复制**：POST_BUILD 复制到 `$<TARGET_FILE_DIR>`，Windows/Linux 上与 dylib 同目录，冗余但无害。
- **平台分支**：`#ifdef Q_OS_MACOS / #else`（#else 同时覆盖 Windows 与 Linux）。
- **GL 调用**：全部为 3.3 core 子集 + Qt 抽象（QOpenGLShaderProgram/VBO/VAO），三平台一致。
- **无平台特有依赖**：无 GLUT/GLEW/GLAD/GLU；`uintptr_t` 在 rhi_gl.cpp 中原本已使用（MSVC 兼容）。

⚠️ 本机只能编译 macOS。Windows/Linux 需要在对应平台各跑一次完整构建 + 运行确认。
重点回归项：2D 台面/网格显示、3D 视图、2D↔3D 工作台切换。

## 9. 3D 视图模型"空洞"（2026-08-14 修复）

### 症状

3D 视图（`RenderWidget3D`）中，导入的 OBJ、BRep 生成的球/立方体均出现"空洞"：

- 模型部分表面缺失/变暗，旋转视图时空洞位置移动，某些角度又自动补上；
- 选中模型时黄色线框高亮**完整无洞**（与填充不一致）；
- 仅 macOS 出现（Apple Metal GL 驱动），Windows 4.6 上完全正常。

### 排查过程（三层根因逐层排除）

1. **剔除（Culling）**：`initializeGL` 原来 `glEnable(GL_CULL_FACE)`。
   导入模型（CAD 导出）绕序不可靠，一半面被剔除 → 改为 `glDisable(GL_CULL_FACE)` 双面渲染。
   排查中发现 BRep 盒子 x=0/y=0/z=0 三个面的三角形法线指向盒内（绕序反向），若开启剔除必缺面。
2. **Apple GLSL 属性位置乱序**：离屏 GL 复现程序（`gltest`）发现 Apple 编译器给 mesh 着色器
   分配的属性槽是 `aPos=1, aNormal=0`（与代码假设相反），`enableAttributeArray(0)` 绑错了数据
   → 法线/位置互换。修复：mesh/line 顶点着色器显式声明 `layout(location = 0/1)`。
3. **光照 + 深度（最终根因）**：见下。

### 最终根因：QPainter 污染 GL 状态（每帧深度测试被关闭）

F9 临时转储（`glGetBooleanv`）显示按键时刻 `DEPTH_TEST=0, BLEND=1`：

- `paintGL` 每帧最后一步是**坐标轴指示器**（`renderAxisIndicator`，用 QPainter 绘制）；
- macOS 上 Qt 的 QPainter GL 引擎会**禁用深度测试、开启混合**，且绘制结束后**不恢复**
  （core profile 已移除 `glPushAttrib`/`glPopAttrib`，Qt 无法按旧方式保存/恢复状态）；
- 结果：下一帧 `renderEntities()` 在**无深度测试**状态下绘制模型 →
  三角形按 VBO 顺序无竞争地叠画，背面/暗面（明暗交界带，法线与光线垂直处亮度仅 ~51，
  背景 42，几乎不可见）盖在正面之上 → 斑驳暗区 = "空洞"，且随视角变化；
- Windows 的 Qt QPainter GL 引擎行为不同（能正确恢复状态），故只在 Mac 出现。

离屏验证（同一份真实球体数据、同一着色器、同一相机）：

| 深度测试 | 圆盘内亮度 <90 的像素（"像洞"） |
|---|---|
| 开启（正确） | **0** |
| 关闭（污染后） | **3172（7.4%）** |

### 修复清单

`UI/3D/Src/Render/RenderWidget3D.cpp`：

1. **`paintGL()` 每帧开头显式重置 GL 状态**（关键修复，防一切外部污染）：
   ```cpp
   glEnable(GL_DEPTH_TEST);
   glDepthFunc(GL_LESS);
   glDisable(GL_CULL_FACE);
   glDisable(GL_BLEND);
   ```
2. **mesh 着色器双面光照 + 防护**（`initPrograms()`）：
   - `float diff = abs(dot(N, L));` — 背光面同样受光（闭合模型任何角度都完整）；
   - `N/V/H` 均加 `if (!(dot(X,X) > 1e-6)) X = vec3(0,0,1); else X = normalize(X);`
     — 防零向量/NaN 导致片元变黑。
3. **mesh 着色器最低亮度下限**：
   ```glsl
   color = max(color, vec3(0.45));
   ```
   明暗交界带（|N·L|≈0）亮度从 ~51 提升到 ≥115，与背景 42 清晰区分，不再像"洞"。
   离屏验证：修复前圆盘内 1532 个像素亮度 60~100（像洞）→ 修复后 0 个。
4. **网格/坐标轴不写深度**：`drawGrid()/drawAxis()` 绘制时
   `glDepthMask(GL_FALSE)` — 网格是参考线，禁止其深度遮挡模型（网格平面以下/共面的模型部位）。
5. **顶点着色器 `layout(location = 0/1)`**：mesh（aPos/aNormal）与 line（aPos/aColor）均显式声明。
6. `initializeGL`：`glDisable(GL_CULL_FACE)` 双面渲染（替代原先的 enable）。

`Engine/3D/Src/Loader/ObjLoader.cpp`（数据层加固，与平台无关）：

- 负索引 `vn` 支持（`f v/vt/vn` 中 vn 可为负数，从末尾倒数）；
- 退化三角形（零面积）跳过：`cr.length() < 1e-12f` 时 `continue`；
- 无法计算法线时回退 `(0,0,1)`，避免 `normalize(0,0,0)` 产生 NaN。

### 验证方法

- 离屏 GL 复现程序（仓库外临时目录）：加载真实 OBJ / `GmcMesh::tessellate` 数据，
  用应用同款着色器/相机渲染并统计"像洞"像素（亮度 <90）。
  注意 `glReadPixels` 行序是**底部向上**，像素分析时需翻转，否则结论会镜像出错。
- 应用内 F9 临时转储 GL 状态（`glGetBooleanv`）确认污染源；发布前已移除。

### 跨平台兼容性（Windows/Linux）

本次全部改动均为**标准 OpenGL 3.3 core 子集**，Windows（4.6 core）与 macOS（4.1 core）完全一致：

- `glDepthMask/glDepthFunc/glEnable/glDisable` — 双平台核心 API，无差异；
- `layout(location = ...)` — GLSL 330 core 双平台支持（macOS GLSL 4.10、Windows GLSL 4.60）；
- `abs(dot(N,L))`、`max(color, vec3(0.45))`、NaN 防护 — 纯 GLSL，双平台行为一致；
- 每帧重置 GL 状态 — 最佳实践：Windows 上状态本就正确（重置为幂等操作，无副作用），
  且防任何未来第三方/Qt 绘制对状态的污染，双平台都更健壮；
- `setUpdateBehavior(NoPartialUpdate)` 仅在 `#ifdef Q_OS_MACOS` 分支（Windows 保持 PartialUpdate）；
- QPainter 状态污染在 Windows 上不存在，但每帧重置后双平台行为统一，无需平台分支。

⚠️ 本机只能编译 macOS。Windows 需在 Windows 上完整构建 + 运行回归：
导入 OBJ、MakeBox/MakeSphere、旋转视角确认无空洞、选中高亮线框完整。

---
