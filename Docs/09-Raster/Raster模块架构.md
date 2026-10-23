# Raster 模块架构

> **性质**：标准/规范文档。
> **范围**：`CAD/Raster/` —— 面向激光雕刻 / 切割工艺的位图处理库。
> **关联**：[`../01-当前架构/C-ABI风格约定.md`](../01-当前架构/C-ABI风格约定.md)、
> [`../09-视觉系统/视觉模块架构.md`](../09-视觉系统/视觉模块架构.md)。

---

## 1. 模块定位

Raster 把一张**位图**变成一份**工艺结果**：要么是给激光控制器烧的 1 bit 工艺图，
要么是给刀路模块走的切割路径。

```
客户素材 / 相机扫描
        │
        ▼
   ┌─────────┐
   │ Raster  │  预处理 → 二值化 → 半调 / 轮廓化 → 质检
   └─────────┘
        │
        ├──► 工艺位图（1 bit / 8 bit）  ──► 激光控制器
        └──► 切割路径（轮廓多边形）    ──► 刀路模块 → 机床
```

### 1.1 与 Vision 的边界

| | Vision | Raster |
|---|---|---|
| 触发方式 | 相机实时帧 | 文件 / 扫描件 |
| 时间尺度 | 在线（ms 级） | 离线（百 ms 级） |
| 核心对象 | 相机、标定、坐标系 | 位图、像素、连通域 |
| 相互依赖 | **无** | **无** |

两者都基于 OpenCV，但**不共享任何代码或头文件**。原因是二者的交付对象不同：
Vision 是设备侧软件的一部分；Raster 是要单独打包给客户 / 第三方软件的库，
两者混在一起会让"能不能单独给客户"这个问题失去答案。

---

## 2. 跨 DLL 契约（硬约束）

### 2.1 规则

1. 公开头**只含 POD、句柄、枚举**：没有 `std::string` / `std::vector` / `cv::Mat` /
   异常 / 虚函数 / 模板。
2. 所有"入参结构体"首字段固定为 `structSize`；库端用它识别调用方的结构体版本。
3. 枚举显式指定底层类型 `int32_t`；布尔语义用 `int32_t`。
4. 字符串出参用 `(char* buffer, size_t bufferSize)`；返回 `const char*` 的函数
   只用于静态存储期的只读文本。
5. 资源一律用**不透明句柄**：库持有实体，调用方只持有指针，显式释放。

### 2.2 为什么必须这么严

老模块的失败路径很典型：公开头里直接写 `cv::Mat` / `std::string`，
本地编译通过、单测通过、客户那边用不同 CRT / 编译器构建消费程序时炸，
现场表现是"随机崩在别的模块的 `free()` 里"。

这类缺陷**加进去时不会报任何错**，只能在真实交付场景暴露，且几乎无法回溯。
所以规则必须由构建系统强制，而不是靠 code review 记性。

### 2.3 强制手段：配置期 ABI 守卫

`Raster/cmake/RasterAbiGuard.cmake`：

- 扫描 `Include/Raster/` 下所有头文件，**先剥注释再匹配**（注释里大量出现
  "不要用 std::string" 这类说明文字，直接匹配会把说明本身报成违规）；
- C ABI 头命中 `std::` / `cv::` / `opencv2/` / `throw` / `dynamic_cast` /
  Qt 类型 → `FATAL_ERROR`，配置直接失败；
- `RasterCpp.h` 命中 `RASTER_API` → `FATAL_ERROR`（它必须保持 header-only inline）。

违规在**本地 configure 阶段**就暴露，不会流到 CI 之后的现场。

---

## 3. "STD 导出问题"与本模块的解法

### 3.1 问题回顾

DLL 导出带 `std::string` 的符号有两重风险：

| 风险 | 表现 |
|---|---|
| 编译期 | MSVC C4251：`needs to have dll-interface to be used by clients of class` |
| 运行期（更致命） | 调用方与 DLL 的 `_ITERATOR_DEBUG_LEVEL` / CRT / 编译器不一致时，`std::string` 内部布局不同，**析构时崩在另一个模块** |

第二重风险才是真正的坑：它不报错、不崩溃在出错的地方，只在"用着用着"时炸。

### 3.2 解法：把 STL 关在调用方

```
        调用方 TU                         Raster.dll
  ┌───────────────────────┐         ┌────────────────────┐
  │ RasterCpp.h (inline)  │  C ABI  │  cv::Mat / std::... │
  │  std::string 路径      │ ──────► │  句柄表 / 算子实现  │
  │  RAII 句柄             │  POD    │  只导出 C 函数      │
  └───────────────────────┘         └────────────────────┘
        STL 只活在这里                STL 只活在这里
```

- `RasterCpp.h` 是**纯头文件**：所有类都是 `inline`，没有任何一行代码编进 DLL，
  也没有任何一个带 `RASTER_API` 的符号；
- `std::string` / `std::unique_ptr` / `std::vector` 的生命周期完全在调用方掌控；
- DLL 侧只暴露 POD 与句柄，跨边界的每一个字节都有确定解释。

代价是这些 inline 函数在每个使用方 TU 里重新编译一次 —— 对一层胶水而言可以接受。

---

## 4. 内部架构

```
                 Src/Core
    ┌──────────────────────────────────────────┐
    │ RasterStatus.cpp   版本/状态码/错误信息   │
    │ RasterImage.cpp    句柄生命周期/像素搬运  │
    │ RasterPipeline.cpp 算子表驱动的管线引擎   │
    │ RasterInternal.h   内部共享定义          │
    └──────────────────────────────────────────┘
                 Src/Algo
    ┌──────────────────────────────────────────┐
    │ RasterPreprocess.cpp  通用预处理           │
    │ RasterLaser.cpp       抖动/半调/高度图     │
    │ RasterContour.cpp     轮廓/割缝/走刀顺序   │
    │ RasterInspect.cpp     统计/直方图/质检     │
    │ RasterIO.cpp          文件读写             │
    └──────────────────────────────────────────┘
```

### 4.1 句柄模型

| 句柄 | 实体 | 释放函数 |
|---|---|---|
| `RasterImage` | `cv::Mat` | `Raster_DestroyImage` |
| `RasterContourSet` | `std::vector<ContourData>` | `Raster_DestroyContourSet` |
| `RasterDefectSet` | `std::vector<RasterDefect>` | `Raster_DestroyDefectSet` |
| `RasterPipeline` | `std::vector<OpEntry>` | `Raster_DestroyPipeline` |

句柄**不是线程安全的**：同一句柄同一时刻只能被一个线程使用。
并发场景必须先 `Raster_CloneImage`。

### 4.2 错误处理

- 全局状态（最近一次错误）用 **thread_local** 保存：位图处理是典型并行场景，
  用全局缓冲会让 A 线程的错误信息被 B 线程覆盖，比没有错误信息更糟；
- 库内任何 OpenCV 调用抛的异常，在 `raster::internal::guard()` 里被翻译成
  `RasterStatus` + 文本。**异常绝不穿过 DLL 边界**——调用方可能是 C 语言、
  可能是不支持异常的嵌入式运行时。

### 4.3 管线

管线把一串算子固化成**可复现的工艺流程**。同一套前处理要同时跑在调试机、
生产机、客户自研软件上时，必须有一份唯一的定义。

算子参数用统一的 `RasterOpParams`（定长 POD，可直接按二进制存配方），
字段含义逐算子写在 `RasterTypes.h` 的表格里，**新增字段只能追加**。

---

## 5. 关键算法说明

### 5.1 割缝补偿（kerf）

激光切割会烧掉一条宽度为 `kerf` 的材料。按原轮廓走刀，成品尺寸会小约 `kerf/2`。
补偿做法：

1. 把轮廓多边形栅格化成掩码；
2. 形态学膨胀 / 腐蚀 `kerf/2`；
3. 重新提取轮廓并移回原坐标系。

为什么不用顶点法偏移：顶点法在尖角与自交轮廓上会崩；栅格化路线对任意形状都成立。

### 5.2 平场校正

激光相机在暗场下常有固定的照明不均，直接二值化会把背景照度当成图案。
做法是降采样后用大 σ 高斯估计背景，再做除法归一化。

### 5.3 半调输出必须严格二值

网屏回转用了双线性插值，会在边缘引入灰度过渡；而半调结果最终变成 1 bit 雕刻图，
任何中间灰度都会变成多余的烧穿点。因此回转后强制再阈值化一次。

### 5.4 码识别的分辨率前提

实测：二维码在 **1 像素/模块** 时，OpenCV 的检测器能定位到四个角点，但
**解不出内容**；放大到 **2 像素/模块** 即可正常解码。

这条不是理论推导，是本模块开发时实测出来的：定位图案（finder pattern）
的 1:1:3:1:1 比例在单像素下已经无法采样，纠正码的采样同样失效。
因此打标复核照片必须保证码区有足够的像素密度——激光打标的码本身是
毫米级尺寸，正常拍照都远高于这个门槛。

### 5.5 轮廓坐标系

OpenCV 的 `findContours` 返回的坐标原点在**左上角**、y 向下。
本模块统一沿用该约定，并在 `RasterTypes.h` 里写明：
与 CAD 几何坐标（左下原点、y 向上）不一致，转换责任在调用方。

---

## 6. 性能

### 6.1 实测基准

`Tools/benchmark/` 只消费公开 API，测出来的就是客户实际会感受到的性能。
本机（x64，MSVC 19.51，OpenCV 4.12）实测 2000x1500 灰度（3.0 MPix）：

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

结论：
- **NLM 去噪是数量级最慢的算子**（3.5 MPix/s）。扫描件上通常先用中值/双边
  滤波去粗噪，只有在需要保留细节时才上 NLM。
- **轮廓提取次之**（27 MPix/s），且耗时与连通域数量强相关 —— 噪点没清干净
  会让轮廓数爆炸。先去孤点再提轮廓。
- 其余算子都在 100 MPix/s 以上，实时性无忧。

### 6.2 一次被基准逮到的性能缺陷

`Raster_Despeckle` 最初的实现是"每个连通域做一次全图比较
(`labels == label`) + 掩码填充"：

```cpp
for (int label = 1; label < componentCount; ++label) {
    if (tooSmall) cleaned.setTo(cv::Scalar(0), labels == label);   // O(像素数) × O(连通域数)
}
```

复杂度是 **O(连通域数 × 像素数)**。在 3 MPix、上万连通域的图上实测 **39.9 秒**，
把整条工艺流程拖成 85.7 秒 —— 这在产线上就是"机器卡住了"。

改成"先按连通域算一张标签查表，再单趟扫描按查表清像素"后：

| | 优化前 | 优化后 | 倍数 |
|---|---:|---:|---:|
| Despeckle (3 MPix) | 39 870 ms | 37.3 ms | **1070×** |
| 完整工艺流程 | 85 726 ms | 63.4 ms | **1350×** |

教训：**逐连通域的全图操作是隐形的时间黑洞**。只要循环次数与图像尺寸相关，
就必须先把决策收敛成查表，再做一趟扫描。
`RasterLaser.DespeckleHandlesManySmallComponents` 是这个缺陷的回归守卫。

---

## 7. 部署形态

| 形态 | 产物 | 用途 |
|---|---|---|
| 集成进主工程 | `bin/Raster.dll` | CAD 主程序内部使用 |
| 独立 SDK | `bin/` + `lib/` + `include/` + `lib/cmake/` | 交付给第三方，独立 `find_package(Raster)` |

OpenCV 在 vcpkg 下默认是动态库，`Raster.dll` 运行时依赖 `opencv_*.dll`。
需要单文件夹自包含：`cmake --install ... -DRASTER_INSTALL_OPENCV_RUNTIME=ON`
（安装时拷贝 OpenCV 运行时），或用 vcpkg 静态 triplet。

### 7.1 自包含交付的传递依赖

`RASTER_INSTALL_OPENCV_RUNTIME=ON` 会把 OpenCV 运行时一起装到 `bin/`。
**只装直接依赖是不够的**，实测链：

```
Raster.dll
 ├─ opencv_core4 / imgproc4 / imgcodecs4 / photo4 / objdetect4
 │   ├─ opencv_objdetect4 → opencv_dnn4 → libprotobuf → abseil_dll
 │   │                     → opencv_calib3d4 → opencv_features2d4 → opencv_flann4
 │   └─ opencv_imgcodecs4 → jpeg62 / libpng16 / libwebp* / libsharpyuv
 │                         → tiff → zlib1 / liblzma
 └─ MSVCP140 / VCRUNTIME140 / api-ms-win-crt-*
```

少装任何一个，客户那边的程序都会以 `STATUS_DLL_NOT_FOUND (0xC0000135)`
起不来 —— 而且报错完全指不到真正缺的是哪一个。

传递闭包里 opencv_* 之外的部分（protobuf / abseil / webp / sharpyuv / lzma）
在 OpenCV 的模块依赖图（`INTERFACE_LINK_LIBRARIES`）里看不到，
所以由 `Raster/Raster/CMakeLists.txt` 里的**显式名单**补齐。
名单的正确性由**交付门禁**保证（见 7.2）：少一个 DLL，外部消费程序就起不来。

### 7.2 跨平台

模块本身不含平台专有 API。可移植性靠三件事保证：

1. **公开头是纯 C/POD**：不含 Windows 类型、不含 `__declspec` 之外的平台宏
   （导出宏本身按平台分派，消费方无需自己判断）。
2. **`find_package(OpenCV)` 不写死 COMPONENTS**：不同平台 / 发行版对
   OpenCV 组件名的声明不一致（vcpkg 列出全部模块，某些源码构建只导出用到的），
   写死会在某个平台上直接 configure 失败。改成整体找到 OpenCV，
   再自己挑需要的模块目标，缺哪个降级到 `OpenCV_LIBS`。
3. **Unix 的运行时与 RPATH**：
   - vcpkg 下 OpenCV 默认**静态**链接，`libRaster.so` 没有 OpenCV 依赖；
   - 对着系统动态 OpenCV 时，`RASTER_INSTALL_OPENCV_RUNTIME=ON` 把 `.so`
     装到 `lib/`，并给 Raster 设 `INSTALL_RPATH "$ORIGIN"` ——
     `$ORIGIN` 让加载器按"库文件所在目录"解析，
     而不是依赖 `LD_LIBRARY_PATH`（后者对交付给客户的 SDK 是不成立的假设）。

CI：`.github/workflows/raster-cross-platform.yml`（ubuntu-22.04 + macos-14 arm64），
在两个平台上跑「独立构建 + 单元测试 + 装 SDK + 仓库外消费方编译运行」。

### 7.3 交付件

`Tools/package/PackageSdk.cmake`（`cmake -P` 脚本）把装好的 SDK 打成
可交付压缩包，并生成两份随包文件：

- `MANIFEST.txt` —— 每个文件的 SHA-256 与大小。交付出去的二进制是
  **不可复现**的（编译期、工具链、优化选项都会变），收到包的人必须能
  独立验证"这个文件确实是我给你的那个"。
- `VERSION.txt` —— 模块版本、ABI 版本、架构、打包日期、工具链。

打包用 CMake 脚本而不是 PowerShell/shell：交付打包这一步必须在
Windows / Linux / macOS 上给出**同一个结果**，不该在 CI 里维护两套逻辑。

### 7.4 交付门禁

`Raster/Tools/VerifySdk.ps1` 守护的是**交付契约**，不是算得对不对：

| 检查 | 内容 |
|---|---|
| 目录形态 | `include/` 恰好 4 个公开头；内部头 / OpenCV 头一个都不能漏；`bin/` 没有来路不明的 DLL |
| 导出表 | 头里声明的每个 `RASTER_API` 都导出；不多导；**没有任何 C++ 装饰名**（那意味着 STL 漏出了 DLL）；Debug 与 Release 导出一致 |
| 运行时依赖 | 只允许 MSVC 运行库 + OpenCV；出现 Qt / spdlog / 兄弟模块立刻失败；Release 不得导入 Debug CRT |
| 外部消费方 | 仓库外的纯 C 与 C++ 两个工程，只用装好的 SDK 编译并运行 |

CI：`.github/workflows/raster-sdk.yml`。

---

## 8. 扩展指引

新增一个算子需要动四个地方：

1. `Include/Raster/RasterTypes.h`：加枚举值（`RasterOpId`）与参数语义；
2. `Include/Raster/RasterApi.h`：加函数声明（只用 POD）；
3. `Src/Algo/`：实现（用 `raster::internal::guard()` 包起来）；
4. `Src/Core/RasterPipeline.cpp`：在 `applyOp()` 里挂上。

**不要**在公开头里引入 `std::` / `cv::` —— 配置期的 ABI 守卫会直接失败。
需要 STL 的便利接口请写进 `RasterCpp.h`（纯 header-only inline）。

新增测试放在 `Raster/Raster/Test/`，测试图一律程序合成。
