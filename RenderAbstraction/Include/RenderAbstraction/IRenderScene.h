#pragma once
#include "IRenderDevice.h"
#include "IRenderTypes.h"
#include <memory>
#include <vector>

namespace RenderAbstraction {

class IRenderScene {
public:
    virtual ~IRenderScene() = default;

    /// 分配本帧的瞬态顶点内存。仅在 beginFrame / endFrame 之间有效。
    /// 容量不足时返回 false，调用方应丢弃该批而不是画出错误几何。
    virtual bool allocTransient(uint64_t sizeBytes, TransientAlloc& out) = 0;

    /// 提交一批绘制命令。同一帧内可多次调用。
    virtual bool submit(const DrawPacket& packet) = 0;

    /// 设置模型矩阵，作用于本批世界空间命令；传 nullptr 复位为不施加变换。
    virtual void setModelMatrix(const Matrix4x4* matrix) = 0;

    // ---------- 保留式绘制列表 ----------
    //
    // 命令由后端持有，只在图元变化时 upsert 槽位；每帧只提交一次，后端自己做
    // 剔除 / 排序 / 合批。常驻场景走这里，逐帧都变的内容（预览线、覆盖层）走
    // allocTransient + submit。

    virtual DrawListHandle createDrawList(const DrawListDesc& desc) = 0;
    virtual void destroyDrawList(DrawListHandle list) = 0;
    /// 清空全部条目，保留已分配容量
    virtual void clearDrawList(DrawListHandle list) = 0;

    /**
     * @brief 写入 / 更新一个槽位
     *
     * @param slot 调用方自行分配的槽号，用它把渲染条目关联回业务图元。槽号不必
     *             连续；重写同一个槽号即更新，不会新增条目。
     * @param bounds 世界空间包围盒，按列表的 ViewVolumeType 解读。传 nullptr 表示
     *               该条目永不剔除（覆盖层通常如此）。
     */
    virtual bool upsertDrawItem(DrawListHandle list, uint32_t slot,
                                const DrawInstruction& command, const Aabb3* bounds) = 0;

    /// 移除一个槽位。槽号可被后续 upsert 复用
    virtual bool removeDrawItem(DrawListHandle list, uint32_t slot) = 0;

    /**
     * @brief 提交一个绘制列表
     *
     * @param view 本帧可见范围。传 nullptr 关闭剔除，整表全画。
     *             view->type 与列表的 ViewVolumeType 不一致时同样按关闭剔除处理——
     *             判据不匹配会误裁，宁可多画。
     */
    virtual bool submitDrawList(DrawListHandle list, const ViewVolume* view) = 0;

    /**
     * @brief 开始一帧
     *
     * 返回 false 表示本帧起不来，最常见的原因是交换链尺寸已失效：此时应
     * 调整表面尺寸后重试一次，而不是丢弃这一帧。
     */
    virtual bool beginFrame() = 0;
    virtual void endFrame() = 0;

    virtual TextureHandle createRenderTarget(uint32_t width, uint32_t height) = 0;

    /**
     * @brief 读回当前后备缓冲的像素（视图导出 / 截图）
     *
     * 必须在 endFrame **之前**调用：之后后备缓冲已交给呈现引擎，内容不再保证有效。
     * 输出恒为 RGBA8、左上原点、逐行紧凑（rowPitch = width * 4），翻转由后端完成。
     *
     * @param outPixels 至少 width * height * 4 字节
     * @return 区域越界或容量不足时返回 false
     */
    virtual bool readPixels(uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                            void* outPixels, uint64_t capacity) = 0;

    /// 绑定渲染目标。颜色句柄无效时恢复默认交换链（见 RenderTargetBinding）。
    /// 离屏渲染的尺寸由绑定决定，与表面尺寸解耦
    virtual bool setRenderTarget(const RenderTargetBinding& target) = 0;

    /// 从纹理读回像素（离屏渲染的产物）。capacity 是 outPixels 的字节容量
    virtual bool readPixelsFromTexture(TextureHandle texture, uint32_t x, uint32_t y,
                                       uint32_t width, uint32_t height, void* outPixels,
                                       uint64_t capacity) = 0;

    virtual void setCamera(const CameraDesc& camera) = 0;
    virtual void setLighting(const LightingDesc& lighting) = 0;

    virtual FrameStatistics getFrameStatistics() const = 0;
    virtual bool isValid() const = 0;
};

} // namespace RenderAbstraction
