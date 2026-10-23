# Raster 模块变更记录

本文件记录 Raster 模块（位图处理 DLL）的对外可见变更。

版本号遵循语义化版本：

- **MAJOR**：ABI 不兼容变更（公开头或导出函数的签名/语义变化）。
  消费方**必须**重新编译。
- **MINOR**：向后兼容的新增（新增导出函数、新增枚举值）。
  消费方**不需要**重新编译即可继续工作 —— 但 ABI 号会变，
  老的二进制加载新库会失败，所以要**一起**升级。
- **PATCH**：不改变 ABI 的修复（内部实现、文档、构建脚本）。

`RASTER_ABI_VERSION` 在任何 MAJOR/MINOR 变更时递增。
`RASTER_GetAbiVersion()` 可以在运行时查询，
消费方应当检查它是否与自己编译时一致。

---

## [1.0.0] - 2026-10-22

首个正式版本。ABI 版本 1。

### 新增

- **位图预处理**：灰度化、亮度/对比度、CLAHE、直方图均衡、锐化、
  中值/双边/NLM 去噪、形态学开闭运算、形态学梯度。
- **平场校正** `Raster_FlatFieldCorrect`：消除拍照/扫描带来的不均匀光照。
- **二值化**：固定阈值、Otsu 自适应、三角法、均值法；
  支持反色与"暗区为墨"两种极性约定。
- **半色调** `Raster_Halftone`：Floyd-Steinberg 抖动、网点网屏、
   clustered-dot / Bayer 有序抖动。
- **轮廓提取** `Raster_TraceContours`：外轮廓/内轮廓/全部，
  面积与周长过滤，Douglas-Peucker 简化。
- **割缝补偿** `Raster_KerfCompensate`：按激光实际切缝宽度把轮廓外扩/内缩。
  这是"图纸尺寸"与"成品尺寸"一致的关键。
- **缺陷检测** `Raster_DetectDefects`：基于连通域的面积/长宽比/圆度/偏移检测。
- **码识别** `Raster_DetectCodes` / `Raster_VerifyCode`：QR 与一维码，
  带最小质量阈值与期望内容比对。
- **高度图** `Raster_HeightMapFromGray`：灰度 → 归一化高度。
- **管线** `RasterPipeline`：把算子串成可复用的工艺流程。
- **比较与统计** `Raster_CompareImages` / `Raster_ComputeStats`：
  用于"改图前后是否一致"的回归判定。
- **格式** `Raster_LoadImage` / `Raster_SaveImage`：
  PNG / BMP / JPEG / TIFF，单通道与多通道。
- **C ABI**（`RasterApi.h`）：纯 POD + 句柄，公开头可被 C99 编译。
- **C++ 外观**（`RasterCpp.h`）：header-only 的 RAII 封装，需要 C++17。
- **独立 SDK 交付**：`include/` + `lib/` + `bin/` + `lib/cmake/`，
  可直接 `find_package(Raster CONFIG)`。
- **交付门禁** `Tools/VerifySdk.ps1`：核对目录形态、导出表、运行时依赖，
  并用仓库外的 C / C++ 消费方验证可用性。
- **吞吐基准** `Tools/benchmark/`。

### 修复

- `Raster_Despeckle` 的去孤点实现由"O(连通域数 × 像素数)"改为
  "标签查表 + 单趟扫描"，在 3 MPix 图上从 39.9 s 降到 37.3 ms（**1070×**），
  完整工艺流程从 85.7 s 降到 63.4 ms。
  回归守卫：`RasterLaser.DespeckleHandlesManySmallComponents`。

### 已知限制

- OpenCV 运行时默认**不**随包分发；需要单文件夹自包含时用
  `-DRASTER_INSTALL_OPENCV_RUNTIME=ON`。
- Windows 的 `Raster_Despeckle` 目前是单线程实现；超大图（> 50 MPix）
  建议先做区域裁剪。
- `Raster_Dither` 与 `Raster_Halftone` 目前只支持 8 位单通道输入。
