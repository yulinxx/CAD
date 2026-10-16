# Debug Session: startup-crash-pipeline-23-24

## Symptom
SanYiCAD 启动即崩溃，堆栈只有 CrashHandler 自身帧，真正崩溃位置不可见。
日志最后两行是 `pipeline created idx=23` 和 `idx=24`，之后立即崩溃。

## Reproduction
- 直接运行 Release 构建的 SanYiCAD.exe
- 对比正常启动（9月23日日志）：22 built-in pipelines ready → Font ready → UI shell initialized
- 崩溃启动（10月16日）：22 built-in pipelines ready → Font ready → pipeline idx=23/24 → CRASH

## Hypotheses

### H1: Pipeline idx=23/24 的 shader 编译/链接失败，返回无效 RHI::PipelineHandle，后续访问导致崩溃
- **Falsifiable**: 在 `Runtime::createPipelineFromKey` 的 `device->createPipeline(desc)` 后加检查和日志
- **Evidence point**: `rxRuntime.cpp` createPipelineFromKey 末尾

### H2: `defaultShadersFor` 对新组合返回空 ShaderPair，但后续未做空检查直接创建 RHI pipeline
- **Falsifiable**: 在 createPipelineFromKey 里检查 pair.vertex/pair.fragment 是否为空
- **Evidence point**: rxRuntime.cpp defaultShadersFor 返回值

### H3: Renderx 子模块更新引入了新的 VertexFormat + RenderSpace 组合，但 GL shader 源码缺失
- **Falsifiable**: 在 defaultShadersFor 里找不到 shader 名称对应源文件时记录 ERROR
- **Evidence point**: rxRuntime.cpp defaultShadersFor 映射表

### H4: `createPipelineFromKey` 之后立即访问了尚未正确初始化的 pipelineHandle（空句柄被当作有效值 push_back）
- **Falsifiable**: 在 pipelines.push_back 前验证 handle.valid()
- **Evidence point**: rxRuntime.cpp pipeline 创建后的 valid 检查

### H5: `resolvePipeline` 的某个传入参数组合是 RenderWidget 初始化后首次 renderFrame 新遇到的，且 GL shader 编译在 Debug 构建可用但 Release 因优化/编译选项不兼容
- **Falsifiable**: 对比 Debug vs Release 构建下是否都崩溃
- **Evidence point**: 构建配置、shader 编译

## Progress
- [x] 采集崩溃前后日志差异
- [x] 定位崩溃触发点：RenderWidget ensureRenderer → QWidget::update → paintGL → renderFrame 首次渲染时懒创建 pipeline
- [ ] 分析 minidump 获取真实崩溃栈
- [ ] 添加插桩日志确认具体 hypothesis
- [ ] 实施最小修复
- [ ] 验证修复后启动正常
