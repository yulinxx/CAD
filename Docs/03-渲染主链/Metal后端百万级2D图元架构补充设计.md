# Metal 后端百万级 2D 图元架构补充设计

> 定位：补齐《Metal后端实施设计》与《Metal后端Mac开发指南》未覆盖的
> **工业级百万级 2D 图元**所需架构决策。
>
> 前提：M1/M2 基础代码正确（文档已验证），本设计聚焦「从能画图元」
> 到「能画百万图元且性能达标」之间的架构缺口。
>
> 范围：仅 Metal 后端。CPU 空间索引（分层哈希网格）已由
> 《百万级矢量剔除-空间索引设计》完成，本设计不重复。

---

## 0. 现状与差距分析

### 0.1 已达标的部分（来自空间索引设计 §13）

| 指标 | 实测（Release，100万线段） | 判定 |
|------|---------------------------|------|
| 局部缩放（k=1万） | 0.15 ~ 0.82 ms/帧 | ✅ 达标 |
| 平移 | 0.32 ~ 0.93 ms/帧 | ✅ 达标 |
| drawCalls（clustered） | 430 / 2,369 | ✅ 合批有效 |
| zoom out 全图 | 25 ~ 34 ms/帧 | ⚠️ 已知、可接受（LOD 解决） |

**CPU 侧已不是瓶颈**。瓶颈在 GPU 提交效率和资源上传路径。

### 0.2 当前 Metal 后端的性能杀手（按严重度排序）

| # | 问题 | 位置 | 影响 |
|---|------|------|------|
| 1 | `writeTexture` 每次 `waitUntilCompleted` | `metalDevice.mm:884-898` | 字体图集更新 = 每帧多次 CPU-GPU 同步 |
| 2 | `readTexture` 每次 `waitUntilCompleted` | `metalDevice.mm:1070-1071` | 像素读回阻塞渲染管线 |
| 3 | `toResourceOptions` 无条件 `Shared` | `metalDevice.mm:32-40` | Intel dGPU 顶点缓冲走 PCIe |
| 4 | 无 PSO 缓存策略 | `createGraphicsPipeline` | 大量管线切换 = GPU 侧状态刷新 |
| 5 | 单线程命令录制 | `MetalDevice::beginFrame` | 百万图元的命令编码成为 CPU 瓶颈 |
| 6 | 缺少 5 个关键 MSL shader | `src/shader/metal/` | 文本/3D 功能不完整 |
| 7 | 无 GPU-driven 间接绘制路径 | `drawIndirect` stub | 百万图元无法 GPU 剔除 |
| 8 | 无内存预算控制 | 全局 | 无限增长导致显存溢出 |

### 0.3 核心设计原则

1. **不改变公共 ABI**——所有改动在 Metal 后端内部或 RHI 抽象层
2. **不阻塞现有 M1/M2 验收**——架构补充与功能实现解耦
3. **先解决同步瓶颈，再谈优化**——`waitUntilCompleted` 是 #1 敌人
4. **工业级标准 = 可预测的性能曲线**，不是峰值性能

---

## 1. 异步资源上传架构（优先级 1）

### 1.1 问题本质

当前 `writeTexture` 和 `readTexture` 每次调用都新建 command buffer + `waitUntilCompleted`。

**这是什么**：把 GPU 异步操作变成同步阻塞。CPU 等待 GPU 完成才能继续。

**为什么是百万级图元的致命问题**：
- 字体图集每帧可能更新数十次（每帧文本重排）
- 每次更新 = 一次 GPU 同步 stall
- 100 万图元场景下，文本图集更新频率极高
- CPU 在等 GPU 的这段时间里，什么渲染都做不了

### 1.2 架构设计：异步攒批 + 环形同步

```
┌──────────────────────────────────────────────────────────────┐
│                     MetalDevice                               │
│                                                              │
│  ┌───────────────────────────────────────────────────────┐   │
│  │  ResourceUploadQueue（资源上传队列）                    │   │
│  │                                                       │   │
│  │  struct UploadTask {                                   │   │
│  │    TextureHandle texture;                              │   │
│  │    Rect2D region;                                      │   │
│  │    std::vector<uint8_t> pixels;  // 暂存像素数据       │   │
│  │    MTLResourceOptions options;                         │   │
│  │  };                                                    │   │
│  │                                                       │   │
│  │  void enqueue(UploadTask);   // 非阻塞，O(1)           │   │
│  │  void flush();               // 每帧末尾统一提交        │   │
│  └───────────────────────────────────────────────────────┘   │
│                                                              │
│  ┌───────────────────────────────────────────────────────┐   │
│  │  BlitEncoderPool（blit 编码器复用池）                   │   │
│  │                                                       │   │
│  │  per-frame command buffer → blit encoder → commit      │   │
│  │  不 waitUntilCompleted，靠 3 帧 in-flight 自然覆盖     │   │
│  └───────────────────────────────────────────────────────┘   │
│                                                              │
│  ┌───────────────────────────────────────────────────────┐   │
│  │  ReadbackQueue（读回队列）                              │   │
│  │                                                       │   │
│  │  struct ReadbackTask {                                 │   │
│  │    TextureHandle texture;                              │   │
│  │    Rect2D region;                                      │   │
│  │    uint8_t* outPixels;    // 调用方分配               │   │
│  │    size_t rowPitch;                                │   │
│  │    bool (*callback)(void*); // 完成回调               │   │
│  │    void* userData;                               │   │
│  │  };                                                    │   │
│  │                                                       │   │
│  │  void enqueue(ReadbackTask);   // 非阻塞              │   │
│  │  void poll();                  // 每帧检查完成          │   │
│  └───────────────────────────────────────────────────────┘   │
└──────────────────────────────────────────────────────────────┘
```

### 1.3 核心接口设计

```cpp
// metalDevice.h 新增
class MetalDevice {
public:
    // === 异步上传 ===
    // 非阻塞。像素数据被拷贝到内部暂存缓冲。
    void enqueueTextureUpload(TextureHandle texture, const Rect2D& region,
                               const void* pixels, uint64_t pixelBytes);
    
    // 每帧末尾调用。提交所有积压的上传任务到一个 command buffer。
    void flushTextureUploads();
    
    // === 异步读回 ===
    // 非阻塞。完成后通过回调通知调用方。
    void enqueueTextureReadback(TextureHandle texture, const Rect2D& region,
                                 void* outPixels, uint32_t rowPitch,
                                 ReadbackCallback callback, void* userData);
    
    // 每帧末尾调用。检查已完成的读回任务，触发回调。
    void pollReadbacks();
    
    // === 每帧生命周期 ===
    // beginFrame/submitFrame 中自动调用 flush + poll
};
```

### 1.4 关键实现细节

**upload 攒批策略**：

```cpp
// metalDevice.mm 伪代码

void MetalDevice::flushTextureUploads()
{
    if (m_uploadQueue.empty()) return;
    
    id<MTLCommandBuffer> cb = [m_queue commandBuffer];
    id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
    
    for (auto& task : m_uploadQueue)
    {
        // 1. 创建/复用 staging 缓冲（Shared）
        // 2. memcpy 像素数据到 staging
        // 3. blit copyFromBuffer → texture
        // staging 缓冲由环形缓冲管理，不每帧新建
    }
    
    [blit endEncoding];
    [cb addCompletedHandler:^(id<MTLCommandBuffer> buffer) {
        // 释放已完成的 staging 缓冲引用
    }];
    [cb commit];
    // 不 waitUntilCompleted！
    
    m_uploadQueue.clear();
}
```

**staging 缓冲复用策略**：

```cpp
// 环形 staging 缓冲池（3 帧 in flight）
struct StagingPool {
    static constexpr uint32_t kFrameCount = 3;
    std::array<id<MTLBuffer>, kFrameCount> m_stagingBuffers;
    std::array<uint64_t, kFrameCount> m_stagingSizes;
    uint32_t m_currentFrame = 0;
    
    // 按需扩容，但不缩容
    id<MTLBuffer> acquire(uint64_t minSize);
    void release(id<MTLBuffer>, uint64_t size);
};
```

**readback 回调机制**：

```cpp
// MetalSurface::present() 中自动触发
void MetalSurface::present()
{
    // ... existing code ...
    // 1. 提交 command buffer
    [commandBuffer presentDrawable:m_drawable];
    [commandBuffer commit];
    
    // 2. 检查读回完成
    m_device->pollReadbacks();
    
    // 3. 释放信号量
    dispatch_semaphore_signal(m_inFlight);
}

// pollReadbacks 实现
void MetalDevice::pollReadbacks()
{
    for (auto it = m_readbackQueue.begin(); it != m_readbackQueue.end(); )
    {
        if (it->fenceStatus == FenceStatus::Done)
        {
            // 拷贝数据到调用方缓冲
            memcpy(it->outPixels, it->stagingPtr, it->requiredBytes);
            if (it->callback) it->callback(it->userData);
            it = m_readbackQueue.erase(it);
        }
        else
        {
            ++it;
        }
    }
}
```

### 1.5 迁移路径

`writeTexture` 和 `readTexture` 的**旧接口保持签名不变**，内部行为改为异步：

```cpp
// 旧签名不变
RhiResult writeTexture(TextureHandle, uint32_t, const Rect2D&,
                        const void*, uint64_t) override;
RhiResult readTexture(TextureHandle, const Rect2D&, void*,
                       uint64_t, uint32_t*) override;
```

- `writeTexture` → 入队 `m_uploadQueue`，返回 `Ok`
- `readTexture` → 入队 `m_readbackQueue`，返回 `Ok`（数据尚未就绪）
- 调用方如果需要同步读回（兼容性），通过 `waitIdle()` 强制刷入

**调用方适配**：
- RT 层的 `rxFont.cpp`（图集更新）→ 改为异步模式，利用 `flushTextureUploads`
- `rxSession.cpp`（读回像素）→ 可选同步模式：`enqueue` + `waitIdle`
- `MetalSurface::present` → 自动 `flushTextureUploads` + `pollReadbacks`

---

## 2. Intel dGPU 资源上传路径（优先级 1）

### 2.1 问题

当前 `toResourceOptions` 无条件返回 `MTLResourceStorageModeShared`。

- Apple Silicon：统一内存，Shared 和 Private 性能接近
- Intel Mac + 独立显卡：Shared = 系统内存，GPU 每次读都要过 PCIe

### 2.2 架构设计：双路径策略

```cpp
// metalDevice.h 新增
enum class MemoryTier
{
    UnifiedMemory,   // Apple Silicon
    DiscreteGPU      // Intel dGPU
};

MemoryTier detectMemoryTier();
```

```cpp
// metalDevice.mm
MTLResourceOptions toResourceOptions(MemoryAccess access, MemoryTier tier)
{
    if (tier == MemoryTier::UnifiedMemory)
    {
        // Apple Silicon：Shared 零代价，保留直接路径
        return MTLResourceStorageModeShared;
    }
    
    // Intel dGPU：策略取决于 access
    switch (access)
    {
        case MemoryAccess::CpuToGpu:
            // 顶点缓冲：写一次读多次 → Private + blit 上传
            // 瞬态缓冲：每帧写 → Shared（避免 blit 延迟）
            return MTLResourceStorageModePrivate;
            
        case MemoryAccess::GpuOnly:
            // UBO、光照：不映射 → Private（落显存）
            return MTLResourceStorageModePrivate;
            
        default:
            return MTLResourceStorageModeShared;
    }
}
```

### 2.3 Private 缓冲的写入路径

```cpp
RhiResult MetalDevice::writeBuffer(BufferHandle buffer, uint64_t offset,
                                     const void* data, uint64_t sizeBytes)
{
    MetalBufferRecord* record = m_buffers.get(buffer);
    // ...
    
    if (record->desc.access == MemoryAccess::GpuOnly && 
        m_memoryTier == MemoryTier::DiscreteGPU)
    {
        // Private 缓冲不能直接写 contents
        // 方案：暂存 Shared 缓冲 + blit 拷贝
        id<MTLBuffer> staging = m_stagingPool.acquire(sizeBytes);
        uint8_t* dst = static_cast<uint8_t*>([staging contents]);
        memcpy(dst + offset, data, sizeBytes);
        
        id<MTLCommandBuffer> cb = [m_queue commandBuffer];
        id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
        [blit copyFromBuffer:staging sourceOffset:offset
                     toBuffer:record->buffer offset:offset
                        size:sizeBytes];
        [blit endEncoding];
        [cb commit];
        // 不 wait！靠 3 帧 in-flight 覆盖
        return RhiResult::Ok;
    }
    
    // Shared 路径（Apple Silicon 或 CpuToGpu）
    std::memcpy(static_cast<uint8_t*>([record->buffer contents]) + offset, data,
                static_cast<size_t>(sizeBytes));
    return RhiResult::Ok;
}
```

### 2.4 映射路径

```cpp
MappedRange MetalDevice::mapBuffer(BufferHandle buffer, uint64_t offset,
                                     uint64_t sizeBytes)
{
    MetalBufferRecord* record = m_buffers.get(buffer);
    
    if (record->desc.access == MemoryAccess::GpuOnly && 
        m_memoryTier == MemoryTier::DiscreteGPU)
    {
        // Private 缓冲不可映射
        MappedRange range{};
        m_log.error("[metal] mapBuffer: GpuOnly buffer on discrete GPU is not mappable");
        return range;  // 返回空 range，调用方应使用 writeBuffer
    }
    
    // Shared 路径：正常返回 contents
    range.ptr = static_cast<uint8_t*>([record->buffer contents]) + offset;
    // ...
}
```

### 2.5 检测实现

```cpp
MemoryTier detectMemoryTier()
{
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    MTLFeatureSet featureSet = [device featureSet];
    
    // Apple Silicon 家族：GPU 和 CPU 共享同一物理内存
    // 特征集标识：MTLFeatureSet_macOS_GPUFamily1_v1+
    if (@available(macOS 10.15, *))
    {
        if ([device hasUnifiedMemory])
        {
            return MemoryTier::UnifiedMemory;
        }
    }
    
    // 备选检测：通过 GPU 供应商 ID
    // Metal 不直接暴露 vendor ID，但可通过 device name 判断
    NSString* name = [device name];
    if ([name containsString:@"Apple"])
    {
        return MemoryTier::UnifiedMemory;
    }
    
    return MemoryTier::DiscreteGPU;
}
```

---

## 3. GPU-Driven 渲染数据流（优先级 2）

### 3.1 为什么现在就需要

文档《百万级矢量剔除》§14.7 结论："zoom out 全图 25ms 需 LOD 解决"。

但 LOD 是**应用层**的几何抽稀。在 RenderX 侧，还有一个**更根本的问题**：

即使应用层做了 LOD，CPU 侧仍需为每一帧生成 DrawCommand 列表，
并逐个提交给 GPU。对于百万级图元，即使剔除到 1 万条可见，
1 万个 DrawCommand 的 CPU 提交开销仍然显著。

**GPU-driven（间接绘制）的核心价值**：
- GPU 自己决定画哪些图元、画多少次
- CPU 只需提交一个间接参数缓冲
- 100 万图元 = 1 个 `drawIndexedIndirect` 调用

### 3.2 架构设计

```
┌──────────────────────────────────────────────────────────┐
│  GPU-Driven 数据流                                        │
│                                                          │
│  应用层                                                    │
│  ┌─────────────┐    ┌────────────────┐                  │
│  │ DrawList    │───►│ 空间索引查询    │                  │
│  │ (100万条)   │    │ (O(可见)查询)  │                  │
│  └─────────────┘    └───────┬────────┘                  │
│                           │                             │
│                           ▼                             │
│  ┌──────────────────────────────────────┐               │
│  │ Runtime::submitDrawList()            │               │
│  │                                    │               │
│  │ 1. 空间索引查询 → 可见集            │               │
│  │ 2. 构建 IndirectDrawArgs 数组      │               │
│  │ 3. 写入 GPU 间接参数缓冲            │               │
│  │ 4. 一次 drawIndexedIndirect 调用    │               │
│  └──────────────────────────────────────┘               │
│                                                          │
│  GPU                                                       │
│  ┌──────────────────────────────────────┐               │
│  │ 间接参数缓冲                         │               │
│  │ { vertexCount, instanceCount,       │               │
│  │   firstIndex, baseVertex,           │               │
│  │   baseInstance }[]                  │               │
│  │                                    │               │
│  │ 每个可见条目一条 draw 指令          │               │
│  └──────────────────────────────────────┘               │
└──────────────────────────────────────────────────────────┘
```

### 3.3 间接参数缓冲格式

```cpp
// Metal 侧间接绘制参数
struct IndirectDrawArgs
{
    uint32_t indexCount;       // 索引数
    uint32_t instanceCount;    // 实例数（通常 1）
    uint32_t firstIndex;       // 起始索引偏移
    int32_t   baseVertex;      // 顶点偏移
    uint32_t  baseInstance;    // 实例起始
};

// 缓冲布局：连续数组，每个可见条目一条
// Metal 侧：drawIndexedPrimitives:indirectBuffer:indirectBufferOffset:
//   传入 MTLBuffer + offset
```

### 3.4 与现有架构的集成

**当前 `DrawList` 已经维护了可见集**（空间索引查询后的 `visible` 列表）。
只需在可见集构建后，额外做一步：生成 `IndirectDrawArgs` 数组并写入 GPU 缓冲。

```cpp
// DrawList::resolveImpl 新增路径（仅 Metal 后端）
void DrawList::resolveForMetal()
{
    // 1. 空间索引查询（已有）
    resolveImpl(viewBounds);  // 生成 m_visible 列表
    
    // 2. 如果后端支持 indirect draw
    if (m_device->capabilities().indirectDraw)
    {
        buildIndirectArgs();  // 从 m_visible 构建 args 数组
        uploadIndirectArgs(); // 写入 GPU 缓冲
        m_useIndirectDraw = true;
    }
}

// Runtime::flushGeometryStores 中
void Runtime::flush()
{
    // ... existing geometry flush ...
    
    // Metal 专属：提交间接参数缓冲
    if (device->capabilities().indirectDraw)
    {
        flushIndirectDrawArgs();
    }
}
```

### 3.5 间接绘制的绘制流程

```cpp
// MetalCommandList 中（替代现有 drawIndexed）
void MetalCommandList::dispatchIndirect()
{
    if (!m_indirectBuffer.valid()) return;
    
    [m_renderEncoder drawIndexedPrimitivesIndirect:m_indirectBuffer
                                        indirectBufferOffset:0];
}

// 或者按批次分组提交（更灵活）
void MetalCommandList::dispatchIndirectBatched(uint32_t batchCount)
{
    // 同一管线的 draw 放在一起，减少管线切换
    [m_renderEncoder drawIndexedPrimitivesIndirect:m_indirectBuffer
                                        indirectBufferOffset:0];
}
```

### 3.6 过渡策略

**阶段 A（Metal M3）**：`drawIndirect` 启用，但默认关闭。用 `Capabilities.indirectDraw` 控制。
- CPU 路径仍然可用（当前实现）
- GPU 路径通过 `RuntimeDesc::enableIndirectDraw = true` 显式开启

**阶段 B（Metal M4 后）**：默认启用 indirect draw。
- 2D 路径：100 万图元 → 1 个 draw 调用
- 3D 路径：类似

**阶段 C（可选）**：Compute shader 剔除（`culling.comp`）
- 当前 CPU 空间索引已足够（0.15ms）
- Compute 剔除在 >1000 万图元时才有显著优势
- 属 M3 之后独立一轮

---

## 4. 管线状态缓存与分组策略（优先级 2）

### 4.1 问题

`createGraphicsPipeline` 为每个唯一的 `(vertexFormat, space, topology, blend, depth, fillMode, lineWidth, depthBias)` 组合创建一个 `MTLRenderPipelineState`。

当前 `PipelineCache` 已存在（`pipelineCache` 按 `PipelineKey` 缓存句柄），但有两个问题：

1. **MTLRenderPipelineState 的创建是昂贵的 GPU 编译操作**——不应在渲染过程中触发
2. **管线切换的代价 = GPU 侧状态刷新**——应尽量减少切换次数

### 4.2 架构设计：预编译 + 按序提交

```
┌──────────────────────────────────────────────────────────┐
│  PipelineManager（管线管理器）                              │
│                                                          │
│  ┌────────────────────────────────────────────────────┐  │
│  │ 预编译队列（Runtime 初始化时）                       │  │
│  │                                                    │  │
│  │  for (auto& key : kDefaultPipelineKeys)            │  │
│  │      createPipeline(key);  // 同步创建，阻塞        │  │
│  │                                                    │  │
│  │  结果：m_pipelineCache 满载，无运行时编译            │  │
│  └────────────────────────────────────────────────────┘  │
│                                                          │
│  ┌────────────────────────────────────────────────────┐  │
│  │  管线分组器（每帧）                                 │  │
│  │                                                    │  │
│  │  输入：sortKey 排序后的 DrawCommand[]               │  │
│  │  输出：按 pipelineIndex 分组的 draw 列表            │  │
│  │                                                    │  │
│  │  同一 pipeline 的 draw 连续提交 → 0 次管线切换      │  │
│  └────────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────┘
```

### 4.3 管线预编译

```cpp
// Runtime::ensureDefaultPipelines 增强版
bool Runtime::ensureDefaultPipelines()
{
    // ... existing code ...
    
    // Metal 专属：预编译所有默认管线
    // 确保 Runtime 初始化时完成所有 MTLRenderPipelineState 创建
    // 避免渲染过程中首次创建导致卡顿
    
    if (device->capabilities().acceptedShaderLanguage == 
        RHI::ShaderLanguage::MetalLib)
    {
        for (const auto& entry : kEntries)
        {
            uint16_t index = resolvePipeline(...);
            // 确保 MTLRenderPipelineState 已创建
            // （createPipelineFromKey 中已创建，这里只是确保触发）
        }
        
        // 预热：提交一个空的 command buffer 并等待完成
        // 确保所有 pipeline state 已编译完成
        idleWarmup();
    }
    
    return ready == DP::Count;
}
```

### 4.4 管线分组提交

当前 `Session::submitDrawCommands` 已按 sortKey 排序。但排序后是按 `DrawCommand` 顺序逐条提交的。

**改进方向**：在排序后、按 pipelineIndex 分组，每组内部连续提交。

```cpp
// Session::submitDrawCommands 伪代码增强
void Session::submitDrawCommands(const DrawPacket* packet)
{
    // 1. 排序（已有）
    radixSort(m_commands, m_commandCount, sortKey);
    
    // 2. 按 pipelineIndex 分组
    //    当前：逐条提交 → 每次 bindPipeline 都切换管线
    //    改进：同 pipeline 的 draw 连续提交 → 0 次切换
    
    uint16_t currentPipeline = 0xFFFF;
    for (uint32_t i = 0; i < m_commandCount; ++i)
    {
        auto& cmd = m_commands[i];
        
        if (cmd.pipelineIndex != currentPipeline)
        {
            currentPipeline = cmd.pipelineIndex;
            // bindPipeline（这是唯一需要切换管线的时刻）
            commandList->bindPipeline(pipelines[currentPipeline]);
        }
        
        // 提交 draw（无需切换管线）
        if (cmd.indexCount > 0)
            commandList->drawIndexed(...);
        else
            commandList->draw(...);
    }
}
```

### 4.5 与合批的协同

`DrawList` 的合批（`canMerge`）已经保证同状态的 DrawCommand 相邻。
排序后，同 `pipelineIndex` 的条目自然连续。

**关键约束**：合批后的 `DrawCommand` 的 `pipelineIndex` 必须正确反映合并后的管线。
当前实现中，合批后的 `pipelineIndex` 由第一个条目决定——这是正确的。

---

## 5. 命令缓冲多线程录制（优先级 3）

### 5.1 问题

当前 `MetalDevice::beginFrame` 返回单一 `MetalCommandList`，所有录制在主线程完成。

对于百万级图元，即使 CPU 剔除后只剩 1 万条可见，
1 万条 DrawCommand 的 `bindPipeline` + `bindVertexBuffer` + `drawIndexed` 调用
仍然有可观的 CPU 开销。如果能并行录制多个 command buffer，
可以分配到多个 CPU 核心。

### 5.2 架构设计：帧级并行 + 合并提交

```
┌──────────────────────────────────────────────────────────┐
│  MetalDevice（线程安全的命令缓冲管理）                     │
│                                                          │
│  ┌────────────────────────────────────────────────────┐  │
│  │ CommandBufferPool（命令缓冲池）                      │  │
│  │                                                    │  │
│  │  m_commandBuffers[kMaxFramesInFlight]               │  │
│  │  每个帧独占一个 command buffer                      │  │
│  │  支持多线程录制同一个 command buffer                 │  │
│  └────────────────────────────────────────────────────┘  │
│                                                          │
│  ┌────────────────────────────────────────────────────┐  │
│  │ ThreadSafeCommandList（线程安全的命令列表）          │  │
│  │                                                    │  │
│  │  • 多个线程可同时录制                                │  │
│  │  • 内部用 mutex 保护 encoder 状态                   │  │
│  │  • 但 draw 调用本身是轻量的（只设置 state + issue）   │  │
│  └────────────────────────────────────────────────────┘  │
│                                                          │
│  ┌────────────────────────────────────────────────────┐  │
│  │ ParallelRecorder（并行录制器）                       │  │
│  │                                                    │  │
│  │  输入：可见 DrawCommand[]                           │  │
│  │  输出：多个子命令缓冲                                │  │
│  │                                                    │  │
│  │  1. 按 pipelineIndex 分组                           │  │
│  │  2. 每组分配一个线程                                │  │
│  │  3. 各线程独立录制自己的 draw 列表                   │  │
│  │  4. 合并到主 command buffer                         │  │
│  └────────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────┘
```

### 5.3 实现约束

Metal 的 `MTLRenderCommandEncoder` **不是线程安全的**。
但 `MTLCommandBuffer` **可以**被多个 encoder 并行录制（通过 `dispatch` 到不同队列）。

**推荐方案**：使用多个 `MTLCommandBuffer` + 合并提交，而非共享一个 encoder。

```cpp
// 简化方案：帧内多个 command buffer 合并提交
class MetalDevice {
    // 每个帧 N 个子 command buffer（N = CPU 线程数或管线组数）
    std::array<id<MTLCommandBuffer>, kMaxSubBuffers> m_subCommandBuffers;
    std::array<MetalCommandList*, kMaxSubBuffers> m_subLists;
    
public:
    // beginFrame 返回一个合并器
    MetalCommandListMerger beginFrameMulti(ISurface* surface);
};

// MetalCommandListMerger：把多个子命令缓冲合并提交
class MetalCommandListMerger {
public:
    MetalCommandList* acquireSubList(uint32_t index);
    void submitAll();  // 按序提交所有子 command buffer
    void present();    // 统一 present
};
```

### 5.4 启用条件

```cpp
// 仅当可见 DrawCommand 数超过阈值时启用并行录制
const uint32_t kParallelThreshold = 500;  // 500 条以上才并行

bool shouldUseParallelRecording(uint32_t drawCount)
{
    return drawCount > kParallelThreshold && 
           m_device->capabilities().maxFramesInFlight >= 3;
}
```

### 5.5 与现有架构的关系

- `MetalSurface::present()` 保持不变——最终仍是 1 个 `commit` + `presentDrawable`
- `MetalDevice::beginFrame()` 返回合并器而非单一 command list
- `Session::submitDrawCommands()` 无需感知并行——合并器对上层透明
- **默认关闭**：M3 先启用单线程录制，确认性能达标后再开并行

---

## 6. 缺失 MSL Shader 设计（优先级 1）

### 6.1 缺失清单

| 缺失文件 | 对应 GLSL | 用途 | 优先级 |
|----------|-----------|------|--------|
| `screen_glyph_p2t2c4_frag.metal` | `screen_glyph_p2t2c4.frag` | 屏幕空间字形（R8 覆盖率） | P0 |
| `world_glyph_sdf_p3t2c4_frag.metal` | `world_glyph_sdf_p3t2c4.frag` | 世界空间字形（距离场 SDF） | P0 |
| `mesh_3d_p3n3.vert.metal` | `mesh_3d_p3n3.vert` | 3D 网格顶点 | P1 |
| `mesh_3d_p3n3_frag.metal` | `mesh_3d_p3n3.frag` | 3D 网格片段（光照） | P1 |
| `culling.comp.metal` | `culling.comp` | GPU 剔除 compute | P2 |

### 6.2 Glyph Shader 设计

#### `screen_glyph_p2t2c4_frag.metal`

```metal
// 屏幕空间字形片段着色器（R8 覆盖率图集）
//
// 与 GLSL 版 screen_glyph_p2t2c4.frag 逐行对应：
//   - 输入：vUV（图集 UV）、vColor（顶点色）
//   - 从图集 R8 通道读取覆盖率
//   - alpha = coverage * vColor.a
//   - 使用 point_coord 或 vUV 采样
//
// 与 vertex 着色器的交互：
//   - 顶点着色器 screen_tex_p2t2c4_vert.metal 已有 vUV
//   - 此处直接使用 vUV 采样图集

#include "rx_push_constants.metal"

struct RxFragmentIn
{
    float2 vUV [[user(locn0)]];
    float3 vColor [[user(locn1)]];
};

fragment float4 fs_main(RxFragmentIn in [[stage_in]],
                         texture2d<float, access::sample> glyphAtlas [[texture(0)]],
                         sampler glyphSampler [[sampler(0)]])
{
    float coverage = glyphAtlas.sample(glyphSampler, in.vUV).r;
    float alpha = coverage * in.vColor.a;
    if (alpha < 0.01) discard_fragment();
    return float4(in.vColor.rgb, alpha);
}
```

**顶点着色器**：复用现有 `screen_tex_p2t2c4_vert.metal`（P2T2C4 + 屏幕空间）。
`rxRuntime.cpp::defaultShadersFor` 中 `ScreenGlyph` 条目指定的 shader 名字映射：
- 顶点：`screen_tex_p2t2c4_vert.metallib`（已存在）
- 片段：`screen_glyph_p2t2c4_frag.metallib`（**需新增**）

#### `world_glyph_sdf_p3t2c4_frag.metal`

```metal
// 世界空间字形片段着色器（距离场 SDF）
//
// 与 GLSL 版 world_glyph_sdf_p3t2c4.frag 逐行对应：
//   - SDF 原理：fwidth 求导做缩放无关的抗锯齿
//   - coverage = smoothstep(0.5 - fwidth, 0.5 + fwidth, sdf)
//   - 距离场图集：R8 通道存储 SDF 值
//   - 与覆盖率模式不同：SDF 可以缩放无关抗锯齿，
//     一张图集服务所有缩放级别（见 FontDesc::sdfPadding）

#include "rx_push_constants.metal"

struct RxFragmentIn
{
    float2 vUV [[user(locn0)]];
    float3 vColor [[user(locn1)]];
};

fragment float4 fs_main(RxFragmentIn in [[stage_in]],
                         texture2d<float, access::sample> glyphAtlas [[texture(0)]],
                         sampler glyphSampler [[sampler(0)]])
{
    float sdf = glyphAtlas.sample(glyphSampler, in.vUV).r;
    
    // 距离场抗锯齿：缩放无关
    float fw = max(fwidth(in.vUV.x), fwidth(in.vUV.y));
    float coverage = smoothstep(0.5 - fw, 0.5 + fw, sdf);
    
    float alpha = coverage * in.vColor.a;
    if (alpha < 0.01) discard_fragment();
    return float4(in.vColor.rgb, alpha);
}
```

### 6.3 Mesh3D Shader 设计

#### `mesh_3d_p3n3_vert.metal`

```metal
// 3D 网格顶点着色器（位置 + 法线）
//
// 与 GLSL 版 mesh_3d_p3n3.vert 逐行对应：
//   - 顶点已是世界坐标（宿主的 mesh 顶点本来就是世界空间）
//   - 没有 per-draw model 矩阵（3D 网格不随视图变换移动顶点）
//   - 光照在片元内计算（Blinn-Phong，三方向光 + 环境项）
//   - 参数来自 FrameUniforms 块（rx_lighting_3d.glsl）
//
// 顶点布局：P3N3 = float3 位置 + float3 法线
//   attribute(0) = aPos (float3)
//   attribute(1) = aNormal (float3)
//   buffer(30) = pushConstant (RxPushConstants + FrameUniforms)

#include "rx_push_constants.metal"

struct RxVertexIn
{
    float3 aPos [[attribute(0)]];
    float3 aNormal [[attribute(1)]];
};

struct RxVertexOut
{
    float4 position [[position]];
    float3 vNormal;
    float3 vWorldPos;
};

vertex RxVertexOut vs_main(RxVertexIn in [[stage_in]],
                             constant RxPushConstants& pc [[buffer(30)]])
{
    RxVertexOut out;
    float4 worldPos = float4(in.aPos, 1.0);
    out.position = pc.uView * worldPos;
    out.vWorldPos = in.aPos;
    out.vNormal = pc.uView / float4(0, 0, 0, 1) ... ; // 法线变换需 UView 的逆转置
    // 实际实现应参照 GLSL 版 mesh_3d_p3n3.vert
    return out;
}
```

**注意**：Mesh3D shader 需要 `FrameUniforms` 块（光照参数），当前 `rx_push_constants.metal` 只有 `RxPushConstants`。
需要确认 `FrameUniforms` 的 MSL 声明是否存在。如果不存在，需要补充。

### 6.4 Culling Compute Shader

#### `culling.comp.metal`

```metal
// GPU 剔除计算着色器
//
// 与 GLSL 版 culling.comp 逐行对应：
//   - 输入：DrawCommand 数组 + 空间索引参数
//   - 输出：可见索引数组（indirect draw args）
//   - 每个 thread group 处理一批图元
//   - 使用 atomic 计数器写入可见索引
//
// 此 shader 依赖 M3（compute pipeline）落地后才启用。
// 当前为 stub。

#include "rx_push_constants.metal"

kernel void cullMain(
    device const uint* drawCommands [[buffer(0)]],
    device const float4* viewBounds [[buffer(1)]],
    device uint* visibleIndices [[buffer(2)]],
    device atomic_uint* visibleCount [[buffer(3)]],
    uint2 gid [[thread_position_in_grid]],
    uint2 gtid [[threads_per_grid_position]])
{
    uint index = gid.x;
    // 视锥剔除逻辑
    // AABB vs viewBounds 相交测试
    // 如果可见：atomic_fetch_add 并写入索引
}
```

### 6.5 MSL 与 GLSL 的字段布局同步

**关键约束**：MSL 的 `float3` 是 16 字节（simd 类型），GLSL 的 `vec3` 是 12 字节（std140）。

`rx_push_constants.metal` 已经用 `float4` 承载 `vec3 + 标量` 解决了这个问题。
但 `FrameUniforms`（光照参数）可能也有同样的问题。

**检查清单**：
- [ ] `rx_lighting_3d.glsl` 的 `FrameUniforms` 块与 MSL 对应声明逐字节一致
- [ ] `mesh_3d_p3n3` 的法线变换矩阵（法线矩阵 = UView 的 3x3 逆转置）
- [ ] 所有 MSL shader 的 `[[attribute(N)]]` 与 GLSL 的 `layout(location=N)` 一一对应
- [ ] 纹理采样器索引与 `toMetalTextureIndex` 一致

---

## 7. 内存预算与资源驱逐（优先级 3）

### 7.1 问题

当前没有任何 GPU 内存预算控制。对于百万级图元场景：
- 顶点缓冲可能达到数百 MB
- 纹理图集可能达到数十 MB
- 如果不加控制，可能导致显存溢出或系统内存压力

### 7.2 架构设计

```cpp
// MetalDevice 新增
class MetalDevice {
    // GPU 显存预算（可配置）
    uint64_t m_gpuBudgetBytes = 512 * 1024 * 1024;  // 默认 512MB
    uint64_t m_gpuUsageBytes = 0;
    
    // 资源驱逐策略
    void enforceBudget();
    
    // 查询当前预算使用率
    double budgetUsage() const { return (double)m_gpuUsageBytes / m_gpuBudgetBytes; }
};
```

### 7.3 驱逐策略

```cpp
void MetalDevice::enforceBudget()
{
    if (m_gpuUsageBytes <= m_gpuBudgetBytes) return;
    
    // 1. 驱逐最近最少使用的纹理（LRU）
    // 2. 驱逐可重建的缓冲（如瞬态环的过期段）
    // 3. 极端情况下，释放最旧的 MTLRenderPipelineState 缓存
    
    // 注意：驱逐后如果再次被引用，需要重新上传
    // 这对纹理影响最小（图集可重新上传），对顶点缓冲影响大（需重传）
}
```

### 7.4 与现有架构的集成

- `TransientRing` 已经有 3 帧轮换，不需要额外驱逐
- `GeometryStore` 的持久缓冲由应用层管理，不需要驱逐
- 需要驱逐的主要是：**纹理图集** + **临时上传缓冲**
- 驱逐时机：`enforceBudget()` 在 `createTexture` / `createBuffer` 时自动调用

---

## 8. 缺失 MSL 文件的 CMake 构建集成

### 8.1 新增 shader 的 CMake 集成

`Renderx/CMakeLists.txt` 的 `RENDERX_SHADER_SOURCES` 已列出所有 GLSL 文件。
Metal 侧的 `.metal` 文件由 `if(APPLE)` 分支的 GLOB 自动收集：

```cmake
if(APPLE)
    file(GLOB_RECURSE RENDERX_METAL_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/shader/metal/*.metal")
```

**新增 `.metal` 文件无需修改 CMake**——只要放入 `src/shader/metal/` 目录，
CMake 的 GLOB 会在下次构建时自动发现。

### 8.2 验证

```bash
# 配置后检查输出应包含新的 metallib
cmake -S Renderx -B build-mac -DCMAKE_BUILD_TYPE=Debug
# 期望看到：[Renderx] Metal shaders: .../screen_glyph_p2t2c4_frag.metallib;...
```

---

## 9. 分阶段实施计划

### Phase A：异步资源上传（M2 完成后立即开始）

| 步骤 | 内容 | 验收标准 |
|------|------|----------|
| A1 | 实现 `enqueueTextureUpload` / `flushTextureUploads` | 字体图集更新不阻塞 CPU |
| A2 | 实现 `StagingPool`（环形 staging 缓冲复用） | `writeTexture` 不每帧新建 command buffer |
| A3 | 实现 `enqueueTextureReadback` / `pollReadbacks` | 读回不阻塞渲染管线 |
| A4 | 改造 `MetalSurface::present` 自动 flush + poll | 帧末自动执行 |
| A5 | RT 层适配（`rxFont.cpp`、`rxSession.cpp`） | 文本渲染正常，无同步 stall |

### Phase B：Intel dGPU 路径 + 管线预编译

| 步骤 | 内容 | 验收标准 |
|------|------|----------|
| B1 | 实现 `detectMemoryTier` + `toResourceOptions` 双路径 | Intel Mac 上 Private 缓冲正常工作 |
| B2 | 实现 Private 缓冲的 blit 上传路径 | `writeBuffer(GpuOnly)` 不崩溃 |
| B3 | 实现 `mapBuffer` 对 Private 缓冲的错误处理 | 返回空 range 而不是 nil 指针 |
| B4 | 管线预编译 + `idleWarmup` | Runtime 初始化时无运行时编译卡顿 |
| B5 | 管线分组提交（按 `pipelineIndex` 连续提交） | drawCalls 不变但管线切换减少 |

### Phase C：GPU-Driven 渲染路径

| 步骤 | 内容 | 验收标准 |
|------|------|----------|
| C1 | 实现 `IndirectDrawArgs` 缓冲格式 | 间接参数正确写入 GPU |
| C2 | `DrawList::buildIndirectArgs()` | 从可见集生成 args 数组 |
| C3 | `MetalCommandList::dispatchIndirect()` | `drawIndexedPrimitivesIndirect` 调用 |
| C4 | `Runtime` 侧集成（`flushIndirectDrawArgs`） | 100 万图元 → 1 个 draw 调用 |
| C5 | `Capabilities.indirectDraw` 正确上报 | 上层可按能力分支 |

### Phase D：命令缓冲多线程录制

| 步骤 | 内容 | 验收标准 |
|------|------|----------|
| D1 | 实现 `MetalCommandListMerger` | 多子 command buffer 合并提交 |
| D2 | 实现 `ParallelRecorder` | 多线程录制不崩溃 |
| D3 | 集成到 `MetalDevice::beginFrame` | 默认关闭，通过 `RuntimeDesc` 启用 |
| D4 | 性能基准测试（百万图元） | 多线程 vs 单线程对比 |

### Phase E：缺失 Shader + Memory Budget

| 步骤 | 内容 | 验收标准 |
|------|------|----------|
| E1 | 补充 5 个缺失 MSL shader | 文本/3D 渲染正常 |
| E2 | 实现 `enforceBudget` + LRU 驱逐 | 显存使用不超预算 |
| E3 | MSL/GLSL 字段布局一致性验证 | 对比测试通过 |
| E4 | 完整冒烟测试 | 所有默认管线就绪 |

---

## 10. 与现有文档的关系

| 现有文档 | 本文档补充的内容 |
|----------|-----------------|
| Metal后端实施设计 | 补充了 M2 之后的性能架构设计 |
| Metal后端Mac开发指南 | 补充了 Phase A-E 的具体步骤 |
| 百万级矢量剔除-空间索引设计 | 验证了 CPU 侧已达标，本设计聚焦 GPU 侧 |
| LOD多级细节跨层设计 | LOD 解决 zoom out 全图，本设计解决渲染效率 |
| 新渲染架构 | 本设计是 Metal 后端实现新渲染架构的具体路径 |
| 3D 架构现状与约束 | Mesh3D shader 依赖本设计的 Phase E |

---

## 11. 验证方案

### 11.1 性能基准（必须可量化）

| 指标 | 工具 | 方法 |
|------|------|------|
| GPU 帧时间 | Xcode → Capture GPU Frame | 每个 RenderPass 耗时 |
| CPU 编码时间 | `MTLCommandBuffer` 的 GPUStartTime/GPUEndTime | 提交时间与 GPU 开始时间的差 |
| drawCalls 数 | `MetalCommandList` 内计数 | 帧末打印 |
| 管线切换次数 | `MetalCommandList::bindPipeline` 计数 | 帧末打印 |
| 上传字节数 | `writeBuffer`/`writeTexture` 内累加 | 帧末打印 |
| 同步 stall 时间 | `MTLCommandBuffer` 的 `waitUntilCompleted` 次数 | 统计每帧调用次数 |

### 11.2 测试场景

使用百万级 2D 线段场景（`RxIncrementalFixture.DrawListMillionPrimitiveCullingBaseline`）：

| 场景 | 期望 | 当前基线 |
|------|------|----------|
| 局部缩放（k=1万） | 保持 0.15~0.82ms | 0.15~0.82ms |
| 文本图集更新频率 | 无同步 stall | 当前有 stall |
| zoom out 全图 | LOD 解决后 <10ms | 25~34ms |
| Intel dGPU 顶点上传 | 与 Apple Silicon 差距 <2x | 未测 |
| 管线切换次数 | 显著减少 | 当前未统计 |

### 11.3 与 GL 的对比验证

同一场景分别用 GL 与 Metal 渲染：
- 逐像素比对（允许小容差）
- 性能对比：GPU 帧时间、drawCalls、管线切换
- 功能验证：文本渲染、3D 光照、离屏渲染

---

## 12. 风险与缓解

| 风险 | 影响 | 缓解 |
|------|------|------|
| 异步上传导致数据竞争 | GPU 读到未完成的数据 | staging 缓冲生命周期由 3 帧 in-flight 保证；`addCompletedHandler` 安全释放 |
| Intel dGPU 的 blit 上传延迟 | 顶点缓冲可能在 GPU 读取时还在传输 | 3 帧 in-flight 自然覆盖；`Private` 缓冲的 `mapBuffer` 返回空 range 防止 CPU 误写 |
| indirect draw 在 macOS 上不支持 | M1 的 `drawIndexedPrimitivesIndirect` 可能不可用 | `Capabilities.indirectDraw` 控制；不支持时回退 CPU 路径 |
| 多线程录制导致死锁 | mutex 保护不当 | 先用单线程录制确认正确性，再逐步引入并行 |
| MSL shader 字段布局错误 | 纹理读出错乱 | 对比测试锁定布局；`rx_push_constants.metal` 已用 `float4` 规避 |
| 内存预算设置不合理 | 过低导致频繁驱逐 / 过高导致溢出 | 默认 512MB；可通过 `RuntimeDesc::gpuBudgetBytes` 配置 |

---

## 13. 结论

本文档补齐了《Metal后端实施设计》和《Metal后端Mac开发指南》未覆盖的百万级 2D 图元所需的架构决策。

**核心结论**：

1. **CPU 侧已达标**（空间索引 + 合批，0.15ms 局部视图）
2. **GPU 侧有三个必须解决的瓶颈**：同步上传、管线切换、缺少 GPU-driven 路径
3. **优先级排序**：异步上传 > Intel dGPU 路径 > 管线缓存 > GPU-driven > 多线程 > Shader 补全 > 内存预算
4. **所有改动不改变公共 ABI**——在 Metal 后端内部或 RHI 抽象层完成
5. **分阶段实施，每阶段独立验收**——与现有 M1-M4 计划并行，不阻塞

下一步动作：**实现 Phase A（异步资源上传）**。
