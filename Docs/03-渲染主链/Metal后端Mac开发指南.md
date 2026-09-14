# Metal 后端 Mac 开发指南

> 用途：在 macOS 上完成 Metal 后端的编译验证、调试、优化与后续阶段开发。
> 写给需要在 Mac 上继续开发/调试这个后端的工程师。
>
> **当前状态（重要）**：M1（设备/表面/资源）+ M2（管线/2D 绘制）代码已经写完，
> 但**从未经过任何编译器**。作者在 Windows 上开发，无法编译 `.mm` 与 `.metal`。
> 因此本文的第 3 节「预判错误」是全文最该先读的部分。
>
> **建议阅读顺序**：
> 1. §0 现状盘点 + 推进顺序（决定「我该先做什么」）
> 2. §1 环境准备（含 §1.4 从 Windows 搬代码到 Mac）
> 3. §2 分步首次编译 → §3 出错时查表
> 4. §6 审查清单（编译过了但画不对时逐项核对）
> 5. §7 冒烟测试 → §8 优化与改写 → §9 M3/M4
>
> **一条纪律（贯穿全文）**：在 Mac 上**只改 Metal 专属文件**（`.mm` / `.metal` /
> `CMakeLists.txt` 的 `APPLE` 分支 / `src/rt/rxRuntime.cpp` 的 Metal 分支）。
> 共享的 RHI 抽象、GL 后端、RT 层其它部分都同时在 Windows 上构建，
> 动它们会把「Mac 上调通」变成「Windows 上编不过」。详见 §8.5。

---

## 0. 现状盘点

- ✅ **M1 设备与表面**：`MTLDevice` / `CAMetalLayer` 表面 / 3 帧 in flight /
  缓冲 / 纹理 / 采样器 / 绑定组 / 读回 / RenderPass / 视口 / 裁剪
- ✅ **M2 管线与 2D 绘制**：`createShader` / `createGraphicsPipeline` /
  `bind*` / `pushConstants` / `draw` / `drawIndexed` + 18 个 MSL shader +
  构建期 `xcrun metal` 编译链 + RT 层按后端语言装载 shader
- [ ] **M3 高级能力**：compute、indirect draw、离屏渲染（当前是明确报错的 stub）
- [ ] **M4 3D 与收尾**：`mesh_3d_p3n3` 的 MSL 未写、深度范围差异未验证、线框缺口未处理
- [ ] **Metal 冒烟测试**：`RenderxMetalTests` 尚未创建（刻意的，见 §7）
- [ ] **宿主集成**：`UI/2D`、`UI/3D` 的视口控件仍需按平台分支创建 surface

**未验证的部分**：4 个 Metal 后端文件（ObjC++）、18 个 MSL 文件、CMake 的
`xcrun metal` 调用、RT 层的 shader 名字映射。这四类都是「写完没跑过编译器」。

### 0.1 推进顺序（严格按 A→F，不要跳）

| 阶段 | 目标 | 验收标准（不达标就不进下一阶段） | 详见 |
|------|------|--------------------------------|------|
| **A 通链** | 让 MSL 与 `.mm` 过编译器 | 18 个 metallib 全部生成；`RenderX` 目标零错误零警告 | §2 §3 |
| **B 不回归** | 确认 RHI 抽象层没被破坏 | 既有 96 个 Null 后端用例全绿 | §2 Step 3 |
| **C 能画对** | 最小场景在 Metal 上结果正确 | 清屏 / 一个 P3C3 三角形 / 一条折线的读回像素与 GL 一致 | §7 |
| **D 接宿主** | GUI 真的跑在 Metal 上 | 视口控件显示与 GL 逐像素一致，交互（缩放/平移/选择）正常 | §8.4 |
| **E 优化** | 有度量才动手 | 每一项改动都有「改前 / 改后」的数字 | §8.1 §8.2 §8.6 |
| **F 补能力** | M3 / M4 | 能力位与实际可用调用路径一致 | §9 |

**为什么顺序不能颠倒**：

- A 之前写测试，等于在错误的地基上盖楼——测试失败你分不清是后端错还是测试错。
- B 是护栏：Metal 的改动若顺手动了 `rhiCore.h` 或 RT 层，Null 后端会先替你暴露出来，
  比等到 Metal 真机调试便宜得多。
- C 之前**不要碰优化**。当前最大的不确定性是「代码能不能跑」，不是「跑得快不快」。
  此前两次性能优化返工（热冷分离、紧凑 bounds）都是因为「没验证前提就动手」。
- D 之前不要做 E。GUI 没接上时，性能数据没有真实负载，量出来的都是合成场景。

---

## 1. 环境准备

### 1.1 必需工具

```bash
# Xcode Command Line Tools（至少需要它自带的 metal / metallib 工具）
xcode-select --install

# 验证工具链就位
xcrun --find metal
xcrun --find metallib
xcrun metal --version
```

> 如果 `xcrun --find metal` 找不到，说明只装了 CLT 的旧版或没装完整 Xcode。
> 装上完整 Xcode 后在 Xcode → Settings → Locations 里把 Command Line Tools
> 指向它。

### 1.2 CMake 与 vcpkg

项目根 `Renderx/CMakeLists.txt` 要求 **CMake ≥ 4.3**（`cmake_minimum_required`）。
先确认版本，版本不够会让配置阶段就失败，容易误判成代码问题：

```bash
cmake --version          # 需要 >= 4.3
```

**vcpkg 路径的传入方式**（这点容易踩）：项目的变量名是 `VCPKG_DIR`，不是
`VCPKG_ROOT`。它由 `Config.cmake` 按平台给默认值，macOS 分支写死的是：

```cmake
elseif(APPLE)
    set(VCPKG_DIR "/Users/ms/vcpkg" CACHE PATH "VCPKG installation directory")
```

如果你的 Mac 用户名不是 `ms`，或者 vcpkg 不在这个位置，**必须显式传**
`-DVCPKG_DIR=/你的路径/vcpkg`，否则后面 `stb_truetype.h` 找不到会直接
`FATAL_ERROR`。同样的道理，`Config.cmake` 里 `Qt_INSTALL_DIR` 的 macOS 默认值
是 `/Users/ms/Qt/6.11.1/macos`，跑 `Renderx` 独立构建时用不到 Qt，
但如果你以后要构建宿主，这个也要一起改。

vcpkg 需要装好的包（Renderx 独立构建相关）：

```bash
$VCPKG_DIR/vcpkg install stb gtest
```

- `stb` 是**必需**的（`rxFont.cpp` 的字形光栅化）。缺了不是「少个 include」，
  而是配置期 `FATAL_ERROR: 未找到 stb_truetype.h`。
- `gtest` 缺了**不会报错**，只会让测试目标不生成——表现为「配置成功但
  `Build: ENABLED` 旁边没有 Test 目标」，很容易当成构建问题查半天。

### 1.3 验证配置是否成功

```bash
cmake -S Renderx -B Renderx/build-mac \
  -DCMAKE_TOOLCHAIN_FILE=$VCPKG_DIR/scripts/buildsystems/vcpkg.cmake
```

**期望看到**（这是判断 Metal 构建链是否接上的第一个信号）：

```
-- [Renderx] Metal backend: .../src/rhi/metal/metalCommandList.mm;...   <- 2 个 .mm
-- [Renderx] Metal shaders: .../metallib/world_p3c3_vert.metallib;...  <- 18 个
-- [RenderX]
--   Tests: ENABLED
```

⚠️ 若没有 `Metal shaders:` 这一行，说明 `if(APPLE)` 分支没进或 GLOB 没匹配到
`src/shader/metal/*.metal`。先解决这个，再谈编译。

⚠️ 若没有 `Metal backend:` 这一行，说明 `enable_language(OBJCXX)` 或 `.mm` 的
GLOB 有问题——这会导致 `metalDevice.mm` / `metalCommandList.mm` **静默地不参与
编译**，阶段 A 看起来「过了」但运行时全是空实现。这一行必须看到。

### 1.4 把代码从 Windows 搬到 Mac

**必须整仓 clone，不能只拷 `Renderx/`**。原因写在 `Renderx/CMakeLists.txt` 里：

- 第 5 行 `include(${CMAKE_CURRENT_SOURCE_DIR}/../Config.cmake)` —— 依赖上一级
- `include("${SANYI_ROOT}/CMake/SanYiPaths.cmake")` 与 `Utils.cmake` —— 统一入口
  `sanyi_add_shared_library` 在这里
- 第 267 行会尝试 `add_subdirectory(../Log/...)` 提供 `Log` 目标

拿到 Mac 后按 §1.3 用 `-S Renderx` 指到子目录即可（`PROJECT_IS_TOP_LEVEL` 为真，
测试会自动启用，见 `Renderx/CMakeLists.txt` 第 415 行）。

搬代码时注意三件事：

1. **行尾符**。`.metal` / `.mm` / `.h` 若被 Git 做了 CRLF 转换，`xcrun metal`
   可能报出莫名其妙的语法错误（MSL 的编译器对行尾不宽容）。确认：

   ```bash
   git config core.autocrlf     # Mac 上应为 input 或 false
   file Renderx/src/shader/metal/world_p3c3_vert.metal   # 不应出现 CRLF
   ```

2. **不要带上 Windows 的构建目录**。`Renderx/build/` 里的 `CMakeCache.txt` 写死了
   MSVC 的编译器路径，拷过来会让配置阶段直接崩。用新的 `build-mac/`。

3. **`.mm` 与 `.metal` 是否真的在仓库里**。这些文件是从 Windows 端写的，
   提交前如果被某个 `.gitignore` 漏掉，Mac 上 GLOB 就会空。clone 后先确认：

   ```bash
   ls Renderx/src/rhi/metal/          # 应有 2 个 .mm（metalDevice / metalCommandList）+ 2 个 .h
   ls Renderx/src/shader/metal/*.metal | wc -l   # 应为 18
   ```


---

## 2. 第一次编译：务必分步，不要一次性全量构建

一次全量构建会同时暴露 **MSL 语法错误 + ObjC++ 错误 + CMake 打通问题**三类问题，
混在一起无法定位。按下面顺序逐层排除。

### Step 1 — 先单独验证一个 MSL 能编译（最快反馈）

这一步绕开 CMake 和 ObjC++，只看 MSL 本身对不对：

```bash
cd Renderx/src/shader/metal
xcrun -sdk macosx metal -std=macos-metal2.0 -I. -c world_p3c3_vert.metal -o /tmp/t.metallib
```

**这一条命令通过后**，再批量验证全部 17 个：

```bash
for f in *.metal; do
  [ "$f" = "rx_push_constants.metal" ] && continue
  echo "=== $f"
  xcrun -sdk macosx metal -std=macos-metal2.0 -I. -c "$f" -o "/tmp/${f%.metal}.metallib" || echo "FAILED: $f"
done
```

> 说明：`rx_push_constants.metal` 是被包含的共享声明，不单独编译（CMake 里已 `continue`）。

### Step 2 — 只编译 Metal 后端，不碰其它

```bash
cmake --build Renderx/build-mac --target RenderX --config Debug
```

先看 CMake 的 `[Renderx] Compiling Metal shader xxx.metal` 是否逐个成功，
再看 ObjC++（`.mm`）的编译错误。

### Step 3 — 跑既有测试，确认没有回归

```bash
cmake --build Renderx/build-mac --target RenderxGLTests RenderxTests -j
# 可执行文件位置取决于生成器：Xcode 在多配置子目录、Makefiles/Ninja 直接在 Test/ 下。
# 不确定时直接找，比记路径可靠：
find Renderx/build-mac -name 'RenderxGLTests' -type f -perm +111
```

然后跑 `RenderxGLTests` 与 `RenderxTests`。这些用例跑的是 **Null 后端**，
与 Metal 无关，**应当全绿**（变更前基线 96 个用例）。它们全绿说明 RHI 抽象层
没有被 Metal 的改动破坏——这是 §0 阶段 B 的验收条件。

---

## 3. 预判错误与修法（重点）

下面按「我审查过、最可能出问题」的顺序排列。每条都给了判断依据和改法。

### 3.1 MSL 相关

| 现象 | 原因 | 改法 |
|------|------|------|
| `-std=macos-metal2.0` 报 unknown argument | 不同 Xcode 版本的可用标准不同 | 改 `macos-metal2.1` / `macos-metal2.2` / `macos-metal3.0`，或直接删掉 `-std=` 用默认 |
| `'Metal/Metal.h' file not found` | MSL 里误包含了 C++/ObjC 头 | MSL 的 include 只应指向同目录的 `.metal`（当前只有 `rx_push_constants.metal`） |
| 结构体字段读到错位的值 | MSL 的 `float3` 是 16 字节，与 std140 的 12 字节不同 | 已规避：`RxPushConstants` 用 `float4` 承载「vec3 + 标量」，**不要改回 float3** |
| `[[point_size]]` 报错 | 写在了片段输出上 | 它只能出现在**顶点**输出结构里（`point_*_vert.metal`） |
| `[[point_coord]]` 报错 | 位置写错 | 它只能是**片段**函数的独立参数，不在 `[[stage_in]]` 结构里 |
| `discard_fragment()` 未声明 | 缺 `metal_stdlib` | 该函数来自 `metal_stdlib`，由 `xcrun metal` 隐式引入；若报错检查文件头是否被误改 |

### 3.2 ObjC++（`.mm`）相关

- **`NSError` 消息转 C 字符串**：代码里用了
  `error != nil ? [[error localizedDescription] UTF8String] : "unknown"`。
  三元表达式两边的类型是 `const char*` 与字面量，类型应能统一；若编译器抱怨
  类型不匹配，改成先取局部变量再传。
- **`dispatch_data_create`**：需要 `#import <dispatch/dispatch.h>`（已在
  `metalDevice.mm` 顶部加）。
- **`__bridge NSView*`**：`SurfaceDesc::window.handleA` 需要确认是 `void*`。
  若不是，这里会报转换错误——按实际类型调整 `__bridge` 的写法。
- **MTL API 的可用版本**：`drawPrimitives:...baseInstance:` 与
  `setDepthBias:slopeScale:clamp:` 需要较新的 macOS SDK。若报
  `unavailable`，说明部署目标（`CMAKE_OSX_DEPLOYMENT_TARGET`）太低，抬高它。
- **ARC**：`.mm` 以 `-fobjc-arc` 编译，**不要**手写 `release`/`autorelease`。
  如果看到「ARC forbids explicit message send of 'release'」，说明误加了手动释放。

### 3.3 CMake 相关

- **metallib 被当成源文件编译**：`RENDERX_SHADER_SOURCES` 里加了
  `${RENDERX_METALLIB_OUTPUTS}`，同时后续有
  `set_source_files_properties(... HEADER_FILE_ONLY TRUE)`。若构建报
  「无法推断 metallib 的语言」，检查这段是否生效。
- **`add_custom_command` 的 OUTPUT 与 EmbedShaders 的依赖**：`EmbedShaders.cmake`
  是脚本模式运行，靠 `DEPENDS ${RENDERX_SHADER_SOURCES}` 拉起点编译。若出现
  「找不到 metallib」，说明依赖没接上，检查 `RENDERX_SHADER_SOURCES` 的 append
  是否在 `string(REPLACE ";" "|" ...)` **之前**（当前顺序是对的，别改动）。
- **EmbedShaders 对 `.metallib` 的语言标签**：`_embed_classify` 按扩展名给出
  `"metallib"`，`shaderLibrary.cpp` 的 `parseLanguage` 已认识这个标签——
  两侧必须一致，改一边要改另一边。

### 3.4 运行时（编译通过但画不出东西时按此顺序查）

1. **shader 找不到**：日志会打
   `[rt] built-in Metal shader "xxx.metallib" not found (N entries embedded)`。
   核对两点：`N` 是否包含全部 18 个；名字是否与 `src/shader/metal/` 下的文件名
   一致（映射规则 `world_p3c3.vert -> world_p3c3_vert.metallib`）。
2. **入口点找不到**：日志
   `vertex entry point "vs_main" not found`。说明 MSL 里的函数名不是
   `vs_main`/`fs_main`，或按 stage 取错了。
3. **顶点数据被当成 uniform 读**（画面是乱七八糟的几何）：buffer 槽位约定冲突。
   确认 MSL 里 pushConstant 用 `[[buffer(30)]]`、顶点用 `[[buffer(0..3)]]`，
   与 `metalCommon.h` 的 `kMetalPushConstantIndex` 一致。
4. **图元错位/用错着色器**：`bindPipeline` 拿到了失效句柄。代码里已会在句柄无效时
   清空绑定并报 error，看日志有没有这条。

---

## 4. 如何在 Mac 上显式启用 Metal

`preferredBackend()`（`rhiFactory.cpp`）目前**刻意不选 Metal**：

```cpp
// 自动选择只考虑已经能承担完整渲染的后端。
// Metal 当前只覆盖到设备/表面/资源（M1）……
```

所以 `Backend::Auto` 在 macOS 上仍会走 OpenGL。要验证 Metal，**显式指定**：

```cpp
RuntimeDesc desc{};
desc.abiVersion = RENDERX_ABI_VERSION;
desc.backend = Backend::Metal;   // 关键：不走 Auto
desc.enableValidation = 1;       // 开启调试层，见 §5
RuntimeHandle runtime = rxRuntimeCreate(&desc);
```

**M2 验证通过后**，把 `preferredBackend()` 里 Metal 的判断提到最前：

```cpp
    BackendKind preferredBackend()
    {
        if (isBackendAvailable(BackendKind::Metal))
        {
            return BackendKind::Metal;
        }
        if (isBackendAvailable(BackendKind::Vulkan))
        {
            return BackendKind::Vulkan;
        }
        return BackendKind::OpenGL;
    }
```

同时要改 `isBackendAvailable(Metal)`——它现在返回什么，取决于
「已编译」还是「能渲染」，这两个是不同的判断（见该文件顶部注释）。

---

## 5. 调试手段

### 5.1 环境变量（最快上手）

```bash
# Metal API 校验层：把非法的 API 用法变成明确报错而不是静默错结果
export MTL_DEBUG_LAYER=1

# 着色器运行时校验（用 MSL 文本路径时才有意义）
export MTL_SHADER_VALIDATION=1

# 打开后每次 newLibrary/newPipelineState 的失败都会带完整 reason
```

### 5.2 抓 shader / 管线编译错误

`createGraphicsPipeline` 失败时，日志里已经带了 `NSError` 的
`localizedDescription`。这是最直接的线索——先看它，再猜。

### 5.3 Xcode Metal Frame Debugger

把 `SanYiCAD`（或一个最小测试宿主）在 Xcode 里跑起来，用
**Debug → Capture GPU Frame**：能看到每个 RenderPass 的附件、每次 draw 的
管线状态、`setVertexBuffer` 实际绑了哪个缓冲、pushConstant 的实际字节。
排查「图像不对」比读日志快得多。

### 5.4 二分定位「坐标错 vs 颜色错」

- 画面**全黑** → 先查 RenderPass 是否真的开起来了（`beginRenderPass` 的日志）
- 画面**有几何但位置不对** → 查 `uView`/`uViewport`（pushConstant 布局，§3.1）
- 画面**颜色通道交换**（红蓝反） → 交换链格式。`CAMetalLayer.pixelFormat` 当前
  取 `SurfaceDesc::preferredColorFormat`，`BGRA8Unorm` 是 Metal 的常规选择

---

## 6. 逐项审查清单（我无法验证，请重点核对）

按「错了会很难查」的优先级：

- [ ] `RxPushConstants` 的字段偏移与 `rxInternal.h` 的 `PushConstants` 一致（128 字节）
- [ ] MSL 里 pushConstant 的 buffer index 恒为 30，与 `kMetalPushConstantIndex` 一致
- [ ] MSL 里 bindGroup 的 buffer index 从 16 起，与 `toMetalBufferIndex` 一致
- [ ] MSL 里纹理 index 与 `toMetalTextureIndex` 一致（`screen_tex_p2t2c4_frag` 用 0）
- [ ] 每个 MSL 文件的顶点属性 `[[attribute(N)]]` 与 GLSL 的 `layout(location=N)` 一一对应
- [ ] `metalEntryPointFor` 的 `vs_main`/`fs_main` 与 MSL 函数名一致
- [ ] `makeMetalShaderName` 的产物名与 CMake 生成的 metallib 文件名一致
- [ ] 视图矩阵的**列主序**约定：GLSL 的 `mat4` 与 MSL 的 `float4x4` 都是列主序，行乘向量
      的写法一致（两边都是 `M * v`）
- [ ] Metal 的 NDC z ∈ [0,1]（GL 是 [-1,1]）——2D 用 z=0 不受影响，**但 M4 做 3D 时必查**

---

## 7. 下一步：先补冒烟测试，再谈优化

M2 代码刻意**没有**配套测试。理由：如果 `.mm` 本身就编译不过，叠加测试代码只会
让问题分不清是后端还是测试。所以顺序是：

1. `RenderX` 编译通过（§2 Step 2）
2. 既有 96 个 Null 后端用例仍全绿（§2 Step 3）
3. **然后**再建 `RenderxMetalTests`，覆盖：
   - 设备创建成功、`Capabilities` 如实上报（`wireframeFill == false`、
     `computeShaders == false`，M3 完成后才打开后者）
   - 清屏 + 读回像素，颜色精确匹配
   - 一条 P3C3 三角形，读回中心像素颜色匹配
   - 离屏渲染到纹理并读回
   - 注意：清屏/读回需要 `ISurface`（`CAMetalLayer` + `NSView`）。在单测里构造
     `NSView` 需要 `NSApplication`，建议把「窗口相关」的验证留给宿主侧的集成测试，
     单测只覆盖不依赖窗口的部分。

### 7.1 与 GL 的对比验证（防「能跑但画错」）

同一场景分别用 GL 与 Metal 渲染，读回像素做逐像素比对（允许小容差，
主要来自抗锯齿与线宽取整）。**这是唯一能发现坐标错位、颜色通道交换一类问题的护栏**
——仅靠「看起来对」无法发现。

---

## 8. 优化与改写方向

以下按「收益 / 风险」排序，建议先做前面几项。

### 8.1 资源上传策略（低风险，可量化）

**现状**：`metalDevice.mm` 的 `toResourceOptions` 无条件返回
`MTLResourceStorageModeShared`（第 32-40 行，连 `access` 参数都用 `(void)` 屏蔽掉了）。

**为什么 M1 这么做是对的、什么时候就不对了**：

- **Apple Silicon（统一内存）**：GPU 与 CPU 同一块物理内存，Shared 几乎零代价，
  而且省掉整条 staging + blit 路径。**这是当前目标平台，不必改。**
- **Intel Mac + 独立显卡**：Shared 缓冲落在系统内存，GPU 每次读顶点都要过 PCIe。
  顶点缓冲是「写一次、每帧读很多次」，这时候 `Private`（落显存）+ blit 上传
  差距很明显。

**改法（三步，可回退）**：

```cpp
// metalDevice.mm
MTLResourceOptions toResourceOptions(MemoryAccess access)
{
    // GpuOnly 的缓冲不会 map，放显存更快；其余的仍走 Shared 以便直接 writeBuffer。
    return access == MemoryAccess::GpuOnly ? MTLResourceStorageModePrivate
                                           : MTLResourceStorageModeShared;
}
```

配套必须改两处，否则会得到**静默的错误结果**而不是报错：

1. `writeBuffer`：Private 缓冲的 `[buffer contents]` 返回 `nil`，`memcpy` 会直接崩。
   必须改成「暂存 Shared 缓冲 + `blitCommandEncoder` 拷贝」。`writeTexture`
   （`metalDevice.mm` 第 856-899 行）已经是这个模式，**照抄即可**——注意它用的是
   device 自己的 `m_queue` + `waitUntilCompleted`，因此**不依赖** M3 的
   `copyBuffer`（那个在 `metalCommandList.mm` 第 524 行还是 stub）。
2. `mapBuffer`：Private 缓冲**不可映射**。必须在 `access == GpuOnly` 时返回空的
   `MappedRange` 并记 error，而不是把 `nil` 当指针返回。

**开关**：加一个 `MemoryAccess` 之外的编译期开关（例如
`RENDERX_METAL_PRIVATE_UPLOAD`）做 A/B 对比，比改代码来回切可靠。

**风险**：`Private` 缓冲在「CPU 写、GPU 正在读」时没有隐式同步。3 帧 in flight
意味着第 N 帧的 GPU 可能还在读第 N-2 帧的缓冲。RT 层的瞬态环形缓冲已经按帧
切片，**持久缓冲**（`GpuOnly` + 每帧重写）才是危险区，改动时要一并确认。

### 8.2 状态冗余消除（中风险）

GL 后端在 `GlCommandList` 里做了绑定冗余消除（管线 / 顶点缓冲+偏移 / 纹理 /
pushConstant memcmp）。Metal 侧目前只做了「同管线早退」与「顶点绑定脏标记」。

Metal 的真正成本不在 `setVertexBuffer` 调用本身，而在**编码器状态切换引起的
GPU 侧管线切换**。因此若要继续优化，方向是：

- 按管线分组提交 draw（合批已经在 RT 层做了，这里是把同一管线的 draw 排在一起）
- 减少 `setRenderPipelineState` 的切换次数，而不是减少 `setVertexBuffer` 次数

**不要**照搬 GL 的粒度去优化，收益来源不同（设计文档 D5 已说明）。

### 8.3 每帧分配与阻塞（低风险）

`MetalSurface::acquireNextImage` 每帧调用 `updateBoundTexture` 更新纹理记录，
不产生分配。但 `createBindGroup` 会 `assign` 两个 vector——如果宿主每帧重建
绑定组，这里会成为热点。**先profile再改**，不要凭直觉动。

另一个更明确的问题：`MetalDevice::writeTexture` 每次调用都自建 command buffer
并 `waitUntilCompleted`（`metalDevice.mm` 第 883-896 行）。图集更新、位图上传
若分散在多处调用，就是把 GPU 往返当同步函数用。改法是攒批：

- 在一次「上传批次」内共用一个 command buffer，批次结束再 `commit` 并统一等待
- 或者干脆异步：`commit` 后不等待，靠 3 帧 in flight 自然覆盖延迟
  （前提是资源生命周期由环形缓冲管理，见 §8.1 的风险段）

**这一条要在 §8.6 的度量之后再做**——先确认它真的是热点。

### 8.4 与宿主的集成（必做，但不在 DLL 内）

Metal 后端只有测试能跑、GUI 用不上，是因为宿主侧还没接：

- `UI/2D`、`UI/3D` 的视口控件需要按平台创建 surface：
  macOS 上传 `SurfaceDesc.window = { NativeWindowKind::CocoaNsView, (void*)widget->winId() }`
  （枚举定义在 `include/render/renderx.h` 第 198-210 行，`handleA` 收 `NSView*`）
- 宿主**不再**需要提供 GL 上下文（当前走 `ForeignGlContext`）
- 这部分改动不在 RenderX 库内，但在「Metal 真正可用」的路径上是必需的

### 8.5 跨平台护栏：Mac 上能改哪些文件

这是最容易出事的一条纪律。**同一个仓库同时在 Windows 上构建**，而在 Mac 上
「能编过」不代表 Windows 上也能编过——反过来，一个为了 Mac 顺手改的共享接口，
会让 Windows 侧立刻断链（历史上出现过「Mac 能编、Windows 链接失败」的偶然依赖）。

按「是否可以动」分三类：

| 类别 | 文件 | 说明 |
|------|------|------|
| **可以放心改** | `src/rhi/metal/*.mm`、`src/rhi/metal/*.h`、`src/shader/metal/*.metal` | 完全不参与非 Apple 构建（`.mm` 在 `APPLE` 分支才 GLOB，`.metal` 同理） |
| **可以改，但要看条件** | `CMakeLists.txt` 的 `APPLE` 分支；`src/rt/rxRuntime.cpp` 里的 backend 分支 | 只要严格限制在 `if(APPLE)` / `language == MetalLib` 内就安全 |
| **不要动** | `src/rhi/rhiCore.h`、`src/rhi/gl/*`、`src/rt/` 的其它部分、`include/render/renderx.h` | 这些是跨平台契约。真的必须改时，**先在 Windows 上改完、编译通过并跑测试**，再同步到 Mac |

**判断方法**：改完之后，回到 Windows 上跑一次

```powershell
cmake -S Renderx -B Renderx/build ; cmake --build Renderx/build --target RenderX
```

如果 Windows 构建失败，说明改到了「不要动」那一栏，**回退并在 RHI 抽象层里
另找落点**（通常是加一个 `Capabilities` 能力位，而不是改接口签名）。

**为什么偏爱加能力位**：`Capabilities` 是「后端如实上报自己能做什么」的机制，
上层按它分支。Metal 的线框缺失（§9 M4）就是靠 `wireframeFill = false` 表达的，
而不是让上层去判断「当前是不是 Metal」。这条原则能同时守住「不依赖业务」
和「低耦合」。

### 8.6 优化必须可度量（否则不要做）

§8.1-§8.4 里每一项都标了「低风险 / 中风险」，但那只是**改动风险**，不代表有收益。
动手前先建立基线，否则改完无法判断是快了还是只是感觉快了。

**测什么**：

| 指标 | 怎么取 | 关注点 |
|------|--------|--------|
| GPU 帧时间 | Xcode → Capture GPU Frame → 每个 RenderPass 的耗时 | 是不是 GPU 瓶颈 |
| CPU 编码时间 | `MTLCommandBuffer` 的 `GPUStartTime`/`GPUEndTime` 与提交时间的差 | 是不是 CPU 在等 GPU |
| draw call 数与管线切换次数 | 在 `bindPipeline` / `draw` 里计数，帧末打印 | Metal 的主要成本来源（§8.2）|
| 上传字节数 | 在 `writeBuffer` / `writeTexture` 里累加 | 验证 §8.1 |

**测场景**：用接近真实负载的数据——即空间索引与 LOD 已经生效的**上百万图元**
场景（见 `百万级矢量剔除-空间索引设计.md`、`LOD多级细节跨层设计.md`）。
用几个三角形的合成场景测出来的数字没有任何指导意义。

**判定纪律**：改动前后各测 3 次取中位数；差异小于 5% 视为无差异，
**不要为无差异的改动增加复杂度**。这是此前两次返工的直接教训。

---

## 9. 后续阶段（M3 / M4）

### M3 — 高级能力（补 GL 4.1 的短板）

- [ ] `createComputePipeline` + `dispatchCompute`（`newComputePipelineStateWithFunction`
      + `dispatchThreadgroups`，入口按 `cs_main` 约定）
- [ ] `drawIndirect` / `drawIndexedIndirect`（`drawPrimitives:indirectBuffer:...`）
- [ ] `copyBuffer` / `copyTextureToBuffer`（`MTLBlitCommandEncoder`）
- [ ] `barrier`：跨编码器（compute 与渲染之间）需要 `MTLBarrier`，
      同编码器内 Metal 自动保证顺序
- [ ] 离屏渲染（`rxTextureCreateRenderTarget` / `rxSessionSetRenderTarget`）
- [ ] 完成后把 `Capabilities::computeShaders` / `indirectDraw` 打开
      （**能力位必须与实际可用的调用路径一致**，否则上层会走进一条只报错的路）
- [ ] 文本图集（覆盖率 R8 / 距离场两种）：还缺
      `screen_glyph_p2t2c4_frag.metal` 与 `world_glyph_sdf_p3t2c4_frag.metal`

### M4 — 3D 与收尾

- [ ] `mesh_3d_p3n3` 的 MSL（顶点 + 片元，Blinn-Phong 光照 + `FrameUniforms`）
- [ ] **深度范围**：GL 裁剪空间 z ∈ [-1,1]，Metal 为 [0,1]。若宿主的投影矩阵按
      GL 约定生成，Metal 上会出现远平面消失或 z-fighting。**这是最隐蔽的一条**，
      症状不报错、只画错。处理方式：在 Metal 侧对 z 做重映射，或让宿主按平台生成矩阵。
- [ ] **线框缺口**：Metal 没有多边形线框模式，`Mesh3DWire` 依赖
      `fillMode = Wireframe`。当前 `createGraphicsPipeline` 会**降级为实心并记 warn**。
      正确做法是上层查 `Capabilities::wireframeFill` 后改走三角化线框。
- [ ] `setDepthBias` 已接（`bindPipeline` 里），验证 3D gizmo 不再 z-fighting

---

## 10. 常见疑问

**Q：为什么每个 GLSL 文件对应一个 metallib，而不是一个 .metal 含 vs+fs？**
A：因为**片段是共享的**。`world_p3c3.frag` 同时服务 `world_p3c3`（世界空间）与
`screen_p3c3`（屏幕空间）两条管线（见 `rxRuntime.cpp` 的 `defaultShadersFor`）。
把顶点与片段打包进同一份库，共享片段就无法被独立引用。

**Q：为什么 pushConstant 用 buffer(30) 这么奇怪的 index？**
A：Metal 的 buffer 参数表是一维的，顶点缓冲与 UBO 共享它。顶点占 0..3，
bindGroup 从 16 起。pushConstant 取最高的 30 是为了避开两者。

**Q：为什么 `readTexture` 不做 y 翻转，而 GL 要翻？**
A：Metal 的纹理原点与 CPU 图像一致（左上），GL 是左下。两者行为不同，需测试锁定。

**Q：`preferredBackend()` 什么时候才能选 Metal？**
A：M2 在真机上验证通过（能画出与 GL 一致的 2D 图元）之后。在那之前选它会得到
最坏结果——能创建设备、画不出东西。
