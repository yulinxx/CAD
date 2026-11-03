# Debug Session: startup-crash-renderframe

## 症状（用户机器）
SanYiCAD 启动后，22 条预热管线全部就绪，RenderWidget 初始化完成，
Font ready (15px) → Font ready (36px)，然后立即崩溃（同毫秒内）。

## 关键时序
```
RenderWidget: backend=OpenGL ... maxLineWidth=1.0  (createRenderer 完成)
Font ready: pixelHeight=15.0                       (TextQuadBuilder 第一次 addText → batchFor → createFont)
Font ready: pixelHeight=36.0                       (第二次 addText → batchFor → createFont)
*** CRASH ***                                      (同一毫秒内！)
```

字体创建是**延迟**的（initialize 只存字节，addText 时才 createFont）。

## 关键发现
1. **Breakpad backtrace 抓错了线程** —— CaptureStackBackTrace 在 Breakpad 的异常处理线程上跑，拿到的全是 CrashHandler/Breakpad 栈。真正崩溃线程的栈丢失了！
2. **Font ready 后同一毫秒崩溃** —— 崩溃点在 createFont 返回后、addText 继续执行 metrics/glyph 取数期间。
3. **本地不崩** —— 可能与特定场景状态有关（用户有恢复的布局快照或打开的文档）。

## 已做修改
1. ✅ 预热管线 LineStrip→Lines（让预热命中 DrawCommand 缓存）
2. ✅ CrashHandlerBootstrap 改进：枚举所有线程 + StackWalk64 打完整栈
3. ✅ rxSession.cpp 加了 recordBatches/draw 插桩 (D3-D6)
4. ✅ rxSession.cpp 加了 beginFrame 插桩 (D3a)

## 当前假设

| ID | 假设 | 观测点 | 状态 |
|----|------|--------|------|
| H1 | batchFor 返回后，addText 里 fontMetrics/fontGlyph 访问 Font* 时崩 | TextQuadBuilder 插桩 | INCONCLUSIVE |
| H2 | writeTexture 上传字体时 GPU 异常（GL 异步问题） | createFont writeTexture 后 | INCONCLUSIVE |
| H3 | Breakpad 在异常处理时**自己**崩了（而非业务代码崩） | SEH handler 能否捕获更早 | INCONCLUSIVE |
| H4 | GL debug output 回调在字体上传期间报 error 导致崩溃 | GL debug callback | INCONCLUSIVE |

## 下一步
用户运行新版本 SanYiCAD → 若再次崩溃，改进后的 CrashHandler 会打印**所有线程**的栈，
从中识别出真正崩溃线程（含业务栈），即可精确定位根因。

---

## 最终结论（2026-10-16 晚）

### 根因
崩溃点为 `rxSession.cpp:866` 的 `runtime->device->gpuMemoryUsageBytes()` 虚调用。
VEH 抓到崩溃指令 `call qword ptr [rdx+0D8h]`（vtable slot 27），但通过探针确认：

- 磁盘 RenderX.dll 的 `GlDevice` vtable 只有 27 个槽（slot 0..26），slot 27 处
  实际是字符串字面量 `"PushConstants..."` 的字节；
- 活内存与磁盘文件一致 → 加载镜像本身没问题；
- **加载的 RenderX.dll 是混编产物**：部分 .obj（rxSession）按旧版
  `rhiGpuDevice.h`（`gpuMemoryUsageBytes` 在 slot 27）编译，另一部分
  （glDevice）按更早版本编译（vtable 无该槽）。虚调用越过 vtable 末尾，
  取到非法目标后崩溃。

混编成因：git bisect 切换 Renderx 子模块 commit（跨越 ba6b538 新增
`readTexture` 虚函数的头文件变更）时，增量构建产生了跨版本混合的 DLL。

### 修复
删除 `build/Renderx/CMakeFiles/RenderX.dir` 全部对象文件，强制 RenderX 全量
重编 → 虚表与调用点布局一致 → 应用正常启动（test-startup.ps1 验证 ALIVE）。

### 教训
- 切换子模块 commit（尤其涉及虚函数表布局的头文件变更）后，增量构建不可信；
  需删除对应库的全部 .obj 强制重编，或直接 clean build。
- VEH 崩溃处理器（异常码 + AV 读写目标 + 崩溃线程栈）保留在主工程中，
  是定位本问题的关键工具，不回退。
- 试用弹窗（LicensingBootstrap 模态框）会阻塞首帧渲染，本地复现崩溃需先
  点掉弹窗（test-startup.ps1 已自动化）。
