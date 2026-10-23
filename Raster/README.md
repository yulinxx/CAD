# Raster —— 位图处理库（激光雕刻 / 切割）

面向**激光雕刻、激光切割**工艺的位图（栅格）处理库。基于 OpenCV，实现为独立 DLL，
对外只暴露**纯 C ABI**（POD + 句柄），第三方工程可以直接调用，不需要 C++ 运行时，
也不需要 OpenCV 头文件。

```
上游：相机扫描 / 客户素材 / 图片导入
   │
   ▼
Raster：预处理 → 二值化 → 半调 / 轮廓化 → 工艺位图 / 切割路径
   │
   ▼
下游：激光控制器（振镜 / 直线电机）、CNC、3D 起伏雕刻
```

---

## 1. 与仓库里 Vision 模块的分工

| | Vision | Raster |
|---|---|---|
| 场景 | 在线机器视觉 | 离线 / 准实时**位图**工艺处理 |
| 输入 | 相机实时帧 | 图片文件、扫描件、客户素材 |
| 核心能力 | 标定、定位、测量、读码 | 二值化、半调、轮廓、割缝补偿、质检 |
| 依赖 | OpenCV + 相机 SDK | 只依赖 OpenCV |
| 彼此关系 | **互不依赖**，可各自单独部署 | |

---

## 2. 目录结构

```
Raster/
├── CMakeLists.txt              独立构建入口（project / 选项 / 示例挂载）
├── Examples/                   对外交付的一部分：4 个示例程序
│   ├── Example_Basic.cpp       载入 → 灰度 → 去噪 → 二值化 → 保存
│   ├── Example_LaserCut.cpp    轮廓提取 → 割缝补偿 → 走刀顺序
│   ├── Example_Pipeline.cpp    管线（可复用的工艺流程）
│   └── Example_CApi.c          纯 C 消费示例（证明 C ABI 可用）
├── Tools/
│   ├── VerifySdk.ps1           ★ 交付门禁：装 SDK + 查导出表 + 跑外部消费方 + 出包
│   ├── sdk-consumer-c/         外部 C 消费方（手工 include/lib 接线）
│   ├── sdk-consumer-cpp/       外部 C++ 消费方（find_package 接线）
│   ├── package/PackageSdk.cmake  跨平台打包（清单 + 压缩包）
│   └── benchmark/              吞吐基准
├── CHANGELOG.md                变更记录（ABI 兼容性约定）
├── ci/CMakeLists.txt           CI 专用入口
└── Raster/
    ├── CMakeLists.txt          DLL 目标、ABI 守卫、SDK 安装
    ├── cmake/
    │   └── RasterAbiGuard.cmake 配置期 ABI 守卫
    ├── Include/Raster/         公开头（对外契约）
    │   ├── RasterExport.h      导出宏（dllexport / visibility）
    │   ├── RasterTypes.h       全部 POD 类型
    │   ├── RasterApi.h         C ABI 函数声明
    │   └── RasterCpp.h         纯头文件 C++ 门面（可选）
    ├── Src/
    │   ├── Core/               句柄、状态、错误、管线
    │   └── Algo/               预处理、激光工艺、轮廓、质检、码识别、IO
    └── Test/                   gtest 单元测试 + 纯 C 兼容性检查
```

---

## 3. 构建

### 3.1 独立构建（推荐：可单独打包给客户）

```bash
cmake -S Raster -B Raster/build
cmake --build Raster/build --config Release
ctest --test-dir Raster/build -C Release
cmake --install Raster/build --config Release --prefix <out>
```

产物（SDK 布局）：

```
<out>/bin/Raster.dll                              # 或 libRaster.so / libRaster.dylib
<out>/lib/Raster.lib                              # 导入库（Debug 为 Raster_d.lib）
<out>/include/Raster/{RasterApi,RasterTypes,RasterExport,RasterCpp}.h
<out>/lib/cmake/Raster/RasterConfig.cmake         # find_package(Raster CONFIG)
```

不需要 Qt、不需要仓库其它模块，只需要 vcpkg 里的 OpenCV。

### 3.2 集成构建（主工程内）

```bash
cmake -B build -S . -DBUILD_RASTER=ON
```

`BUILD_RASTER` 默认 OFF（OpenCV 是重依赖，按需开启）。开启后 `Raster.dll`
与其它模块一起进 `bin/`。

---

## 4. 快速上手

### 4.1 C++（头文件门面）

```cpp
#include <Raster/RasterCpp.h>

raster::Image source = raster::Image::load("part.png");
raster::Image binary = source.toGray().threshold(RASTER_THRESH_OTSU, 0.0, 255.0);

raster::ContourSet contours =
    raster::ContourSet::trace(binary, RASTER_TRACE_ALL, 10.0, 0.5);
raster::ContourSet compensated =
    contours.kerfCompensate(0.15, RASTER_KERF_OUTSIDE).ordered(RASTER_ORDER_INNER_FIRST);

for (int32_t i = 0; i < compensated.count(); ++i) {
    const RasterContourInfo info = compensated.info(i);
    // ... 交给刀路模块生成走刀 ...
}
```

### 4.2 纯 C / ctypes / cgo

```c
#include <Raster/RasterApi.h>

RasterImage src = NULL, gray = NULL;
Raster_LoadImage("part.png", &src);
Raster_ToGray(src, &gray);
Raster_DestroyImage(gray);
Raster_DestroyImage(src);
```

---

## 5. ★ "STD 导出问题"是怎么解决的

这是本模块最关键的设计决定。

### 5.1 问题

如果 DLL 导出一个返回 `std::string` 的函数：

- **MSVC C4251**：`'type' needs to have dll-interface to be used by clients of class`；
- **更危险的是运行期**：调用方与 DLL 的编译器版本、`_ITERATOR_DEBUG_LEVEL`、
  CRT（/MD vs /MT）只要有一处不一致，`std::string` 的内部布局就不一样，
  **释放时崩在另一个模块里**。现场表现是"程序跑着用着随机崩在 `free()`"，
  几乎无法回溯到是哪次提交引入的。

### 5.2 做法

1. **C ABI 只用 POD**：公开头里没有 `std::string` / `std::vector` / `cv::Mat` /
   异常 / 虚函数。所有跨边界的数据都有确定的字节解释。
2. **句柄代替对象**：图像、轮廓集、缺陷集、管线都是不透明指针，
   由库持有实体、调用方只持有指针。
3. **字符串用固定缓冲**：出参一律 `(char* buffer, size_t bufferSize)`，
   返回 `const char*` 的函数只用于"静态存储期、只读、不释放"。
4. **C++ 门面是纯 header-only inline**（`RasterCpp.h`）：
   `std::string` / `std::unique_ptr` 全部关在**调用方的翻译单元**里，
   一个带 STL 的符号都不会被编进 DLL。

### 5.3 强制执行

`cmake/RasterAbiGuard.cmake` 在**配置期**扫描公开头：

- C ABI 头里出现 `std::` / `cv::` / `opencv2/` / `throw` / `dynamic_cast` /
  Qt 类型 → 直接 `FATAL_ERROR`；
- `RasterCpp.h` 里出现 `RASTER_API` → 直接 `FATAL_ERROR`
  （它必须保持 header-only inline）。

违规在本地就暴露，而不是等某个客户用不同编译器构建时才炸。
临时放行用 `-DRASTER_ABI_GUARD=OFF`，但**不要提交这个设置**。

---

## 6. ABI 约定

| 项 | 约定 |
|---|---|
| 链接 | `extern "C"`，Windows 用 `__declspec(dllexport/dllimport)`，Unix 用 `visibility("default")` |
| 版本 | `Raster_GetVersion()` packed 语义版本、`Raster_GetVersionString()`、`Raster_GetAbiVersion()` |
| 状态 | `RasterStatus`，成功值恒为 `RASTER_OK`(0)；`Raster_StatusText()` 取可读文本 |
| 错误详情 | `Raster_GetLastErrorMessage(char*, size_t)`，**线程局部** |
| 出参结构体 | 首字段固定 `structSize`；不匹配返回 `RASTER_E_STRUCT_SIZE`（而不是越界读） |
| 枚举 | 显式 `: int32_t` 底层类型 |
| 布尔语义 | `int32_t`（0/1），不用 `bool` |
| 线程模型 | 全局状态（错误信息）线程局部；单个句柄同一时刻只能被一个线程使用 |

---

## 7. 功能清单

### 7.1 通用预处理

| 函数 | 作用 |
|---|---|
| `Raster_ToGray` | 转灰度 |
| `Raster_Invert` | 反色 |
| `Raster_AdjustBrightnessContrast` | 亮度 / 对比度 |
| `Raster_EqualizeHist` | 全局直方图均衡 |
| `Raster_Clahe` | 自适应直方图均衡 |
| `Raster_Denoise` | 去噪（Non-local Means / 双边 / 中值 / 高斯） |
| `Raster_Sharpen` | 反锐化掩模锐化 |
| `Raster_Threshold` | 阈值化（固定 / Otsu / 自适应 / 三角 / 截断） |
| `Raster_Morphology` | 形态学（腐蚀/膨胀/开/闭/梯度/Top-Hat/Black-Hat） |
| `Raster_Resize` | 缩放（最近邻/线性/双三次/面积/Lanczos） |
| `Raster_Rotate` | 旋转 |
| `Raster_Crop` | 裁剪 |
| `Raster_Flip` | 翻转 |
| `Raster_Deskew` | 倾斜校正（文字 / 图案旋正） |
| `Raster_Canny` | 边缘检测 |
| `Raster_FlatFieldCorrect` | 平场校正（扣掉相机照明不均） |

### 7.2 激光工艺专用

| 函数 | 作用 |
|---|---|
| `Raster_Dither` | 抖动二值化（无 / Floyd-Steinberg / Atkinson / Bayer 8x8 / 聚簇网点） |
| `Raster_Halftone` | 半调网屏（聚簇点 / 线条 / 交叉），照片雕刻用 |
| `Raster_Despeckle` | 去孤点（扫描噪点会变成多余的烧穿点） |
| `Raster_HeightMapFromGray` | 灰度 → 16 位高度图（2.5D 起伏雕刻） |
| `Raster_PackToOneBit` | 打包成 1 bit 位图（激光控制器格式） |

### 7.3 切割路径

| 函数 | 作用 |
|---|---|
| `Raster_TraceContours` | 轮廓提取（外轮廓 / 全部 / 仅孔洞），支持多边形逼近 |
| `Raster_KerfCompensate` | 割缝补偿（外扩 / 内缩 / 居中） |
| `Raster_OrderContours` | 走刀顺序（先内后外 / 先外后内 / 按面积 / 按周长） |

### 7.4 码识别（打标防错 / 追溯）

| 函数 | 作用 |
|---|---|
| `Raster_DetectCodes` | 识别图中的二维码与一维码，返回内容与位置 |
| `Raster_VerifyCode` | 校验码内容是否与期望一致（打错料号立即拦截） |
| `Raster_CodeSetCount` / `Raster_CodeSetGet` | 遍历识别结果 |
| `Raster_DestroyCodeSet` | 释放 |

> 解码前提：二维码至少要有 **2 像素/模块**。1 像素/模块时 OpenCV 能定位到码，
> 但解不出内容（定位图案与数据模块都退化到 1 像素）。
> 激光打标的码在实物上是毫米级尺寸，拍照后天然满足。

### 7.5 统计与质检

| 函数 | 作用 |
|---|---|
| `Raster_ComputeStats` | min/max/mean/std/墨量占比 |
| `Raster_Histogram` | 256 桶灰度直方图 |
| `Raster_DetectDefects` | 缺陷检测（污点 / 划痕 / 孔洞） |
| `Raster_CompareImages` | 两图比对（标定重复性 / 来料一致性） |

### 7.6 管线

`Raster_CreatePipeline` / `Raster_PipelineAdd` / `Raster_PipelineExecute`：
把一串算子固化成**可复现的工艺流程**，可存配方、可下发设备。

---

## 8. 门禁

模块有两道门禁，各管各的事：

| 门禁 | 命令 | 管什么 |
|---|---|---|
| **正确性** | `ctest --test-dir Raster/build -C Release` | 算得对不对 |
| **交付契约** | `powershell -File Raster/Tools/VerifySdk.ps1` | 交付出去的东西长什么样、客户能不能用 |
| **跨平台** | `.github/workflows/raster-cross-platform.yml` | Linux / macOS 上能不能编过、装成 SDK、被外部工程消费 |

三个门禁对应三个 CI workflow：`raster-tests.yml`（正确性）、
`raster-sdk.yml`（Windows 交付契约）、`raster-cross-platform.yml`（Linux/macOS 可移植性）。

### 8.1 正确性门禁

- 87 个 gtest 用例：API 契约、句柄生命周期、预处理、激光工艺、轮廓、质检、码识别、管线；
- 1 个**纯 C** 编译期检查（`c_api_compat.c`）：以 C99 编译公开头，
  证明头文件确实是纯 C 头。第三方用 ctypes / cgo 时遇到的就是同一套约束；
- 所有测试图都是**程序合成**的，不依赖任何外部素材文件。

### 8.2 交付件

一条命令从源码到可交付的压缩包：

```powershell
powershell -File Raster/Tools/VerifySdk.ps1 -Package
```

产出 `Raster-1.0.0-x64-windows.zip`（约 10 MB）：

```
Raster-1.0.0-x64-windows/
├── include/Raster/*.h            4 个公开头
├── lib/ | bin/                   库 + 运行时（含 OpenCV）
├── lib/cmake/Raster/             CMake 包配置
├── share/doc/Raster/
│   ├── NOTICE.txt                第三方归属声明
│   └── examples/*.cpp            4 个示例源码
├── MANIFEST.txt                  每个文件的 SHA-256 与大小
└── VERSION.txt                   模块版本 / ABI / 工具链
```

`MANIFEST.txt` 不是装饰：交付出去的二进制是**不可复现**的，
收到包的人要能独立验证"这个文件确实是我给你的那个"。
客户侧核对一行即可：

```bash
sha256sum -c <(awk '!/^#/ && NF {print $1"  "$3}' MANIFEST.txt)
```

打包脚本是 `Tools/package/PackageSdk.cmake`（`cmake -P` 脚本），
Windows / Linux / macOS 出同一个结果。

### 8.3 交付门禁（VerifySdk.ps1）

这是给"供其它工程调用"这个承诺兜底的检查。它做五件事：

1. **独立构建 + 安装**：从 `Raster/` 独立入口构建，产出完整 SDK；
2. **目录形态**：`include/` 恰好 4 个公开头（内部头、OpenCV 头一个都不能漏出去）、
   `lib/` + `bin/` + `lib/cmake/` 齐全、`bin/` 里没有来路不明的 DLL；
3. **导出表**：头里声明的每一个 `RASTER_API` 函数都被导出，没有多导，
   **没有任何 C++ 装饰名**（`?xxx`）——那意味着有带 STL 的符号漏出了 DLL；
   Debug 与 Release 的导出集合必须一致；
4. **运行时依赖**：只允许 MSVC 运行库与 OpenCV；出现 Qt / spdlog / 其它兄弟模块
   立刻失败；Release 不得导入 Debug CRT；
5. **外部消费方**：在**仓库外**起两个工程，只用装好的 SDK 编译并运行：
   - `Tools/sdk-consumer-c/`：纯 C，手工接 include/lib —— 顺带验证公开头是 C 头；
   - `Tools/sdk-consumer-cpp/`：C++，`find_package(Raster CONFIG)` —— 验证
     安装导出的 CMake 包配置。

> 第 5 条是这里最值钱的一条：它是唯一能证明"客户拿到这个包就能跑"的检查。
> 少装一个 OpenCV 的传递依赖（`opencv_dnn4 -> libprotobuf -> abseil` 这类），
> 消费程序会以 `STATUS_DLL_NOT_FOUND (0xC0000135)` 起不来 ——
> 而这种故障在开发机上**根本不会出现**（开发机的 PATH 上什么都有）。
> 这个门禁就是在交付前把它逼出来。

### 8.4 跨平台可移植性

模块本身不含任何平台专有 API，公开头是纯 C/POD，导出宏按平台分派
（Windows 用 `__declspec`，Unix 用 `visibility("default")`）。

`.github/workflows/raster-cross-platform.yml` 在 **ubuntu-22.04** 与
**macos-14 (arm64)** 上做三件事：

1. 独立构建 + 全部单元测试；
2. 装出 SDK；
3. **仓库外**的 C++ 消费方只靠装好的 SDK 编译并运行 ——
   这一步验证了非 Windows 编译器下公开头依然干净、CMake 包配置里的
   include 目录与库名后缀正确、`.so` 的 `$ORIGIN` RPATH 生效。

Unix 上的两点差异已在代码里处理：

- OpenCV 运行时：vcpkg 下默认**静态**链接，`libRaster.so` 本身没有
  OpenCV 依赖；对着系统动态 OpenCV 时，`RASTER_INSTALL_OPENCV_RUNTIME=ON`
  会把 `.so` 装到 `lib/`，并给 Raster 加 `INSTALL_RPATH "$ORIGIN"`。
- `find_package(OpenCV)` **不写死 COMPONENTS**：不同平台 / 发行版对组件名的
  声明不一致，写死会在某个平台上直接 configure 失败。改成整体找到 OpenCV，
  再自己挑需要的模块目标。

### 8.5 吞吐基准

```bash
cmake --build Raster/build --config Release --target raster_benchmark
./Raster/build/bin_Qt6/Release/raster_benchmark 4000 3000 3
```

输出每个算子的 ms 与 MPix/s，外加一条完整工艺流程的耗时。
基准只消费公开 API，测出来的就是客户实际会感受到的性能。

本机实测（x64 / MSVC 19.51 / OpenCV 4.12，2000x1500 灰度 = 3.0 MPix）：

| 算子 | 耗时 | 吞吐 |
|---|---:|---:|
| Resize 50%（area） | 0.7 ms | 4205 MPix/s |
| ToGray | 1.5 ms | 1982 MPix/s |
| HeightMapFromGray | 3.8 ms | 798 MPix/s |
| Threshold (Otsu) | 7.9 ms | 380 MPix/s |
| FlatFieldCorrect | 10.6 ms | 283 MPix/s |
| Canny | 12.3 ms | 244 MPix/s |
| KerfCompensate | 24.5 ms | 123 MPix/s |
| Dither (Floyd-Steinberg) | 25.5 ms | 118 MPix/s |
| Halftone（点阵网屏） | 37.6 ms | 80 MPix/s |
| Despeckle | 37.3 ms | 81 MPix/s |
| TraceContours | 111 ms | 27 MPix/s |
| Denoise (NLM) | 868 ms | 3.5 MPix/s |

完整工艺流程（平场 → 灰度 → Otsu → 去孤点）：**63 ms / 3 MPix**。

两条可操作的结论：
- **NLM 去噪是数量级最慢的算子**。扫描件先用中值/双边滤波去粗噪，
  只在需要保留细节时才上 NLM。
- **轮廓提取耗时与连通域数量强相关**。噪点没清干净，轮廓数会爆炸 ——
  先去孤点再提轮廓。

> 这个基准不是摆设：它当场逮到一个真实缺陷。`Raster_Despeckle` 原本是
> "每个连通域做一次全图比较 + 掩码填充"，复杂度 O(连通域数 × 像素数)，
> 在 3 MPix 上实测 **39.9 秒**（把整条工艺流程拖成 85.7 秒）。
> 改成"标签查表 + 单趟扫描"后是 **37.3 ms**，快了 **1070 倍**。
> 回归守卫：`RasterLaser.DespeckleHandlesManySmallComponents`。

---

## 9. 依赖与许可

- OpenCV 4.x（`core` / `imgproc` / `imgcodecs` / `photo` / `objdetect`），Apache-2.0；
- 仅在 DLL **内部**使用，公开头不含 `opencv2/`，消费方不需要 OpenCV 头文件。

vcpkg 的 OpenCV 默认是动态库，`Raster.dll` 运行时依赖 `opencv_*.dll`。
需要单文件夹自包含时：`-DRASTER_INSTALL_OPENCV_RUNTIME=ON`
（安装时把 OpenCV 运行时连同传递依赖一并拷过去），
或用 vcpkg 的静态 triplet 构建。

随包分发 OpenCV 二进制时会一并安装 `share/doc/Raster/NOTICE.txt`，
列出 OpenCV 及其可能的传递依赖（protobuf / abseil / libjpeg / libpng /
libwebp / libtiff / zlib / liblzma）的许可归属 —— Apache-2.0 §4 要求
redistributions 保留归属声明。

详细架构说明见 [`Docs/09-Raster/Raster模块架构.md`](../Docs/09-Raster/Raster模块架构.md)。
