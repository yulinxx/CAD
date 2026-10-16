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
