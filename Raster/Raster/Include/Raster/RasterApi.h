#ifndef RASTER_API_H
#define RASTER_API_H

/**
 * @file RasterApi.h
 * @brief Raster（位图处理）模块对外 **C ABI**。
 *
 * ============================================================================
 * 模块定位
 * ============================================================================
 *
 * 面向**激光雕刻 / 激光切割**工艺的位图（栅格）处理库：
 *   - 上游：相机扫描 / 图片导入 / 客户素材；
 *   - 本模块：预处理 → 分割/二值化 → 半调或轮廓化 → 工艺位图 / 切割路径；
 *   - 下游：激光控制器（振镜/直线电机）、CNC、3D 起伏雕刻。
 *
 * 与仓库里 Vision 模块的分工：
 *   - Vision   —— 在线机器视觉（相机、标定、定位、质检流水线）；
 *   - Raster   —— 离线/准实时**位图**工艺处理（二值化、半调、轮廓、割缝补偿）。
 *   两者都基于 OpenCV，但契约完全独立，Raster 不依赖 Vision。
 *
 * ============================================================================
 * 使用约定
 * ============================================================================
 *
 *  1. **纯 C ABI**。跨边界只有 POD、句柄与错误码；不出现 STL / 异常 / 虚函数 /
 *     OpenCV 类型。第三方可用 ctypes / cgo / Delphi / C# P-Invoke 直接调用。
 *  2. **句柄即资源**。RasterImage / RasterContourSet / RasterDefectSet /
 *     RasterPipeline 都是不透明指针，用完必须调用对应的 Destroy* 释放。
 *  3. **不抛异常**。所有失败路径返回 RasterStatus；细节取自
 *     Raster_GetLastErrorMessage()（线程局部缓冲）。
 *  4. **入参结构体带 structSize**。新增字段只能追加，旧调用方不受影响。
 *  5. **线程模型**：
 *     - 库内全局状态（错误信息）为线程局部，多线程可并行调用不同句柄；
 *     - 单个句柄同一时刻只能被一个线程使用；并发场景请先 Clone。
 *
 * ============================================================================
 * 典型调用序列（激光切割轮廓）
 * @code
 *   RasterImage src = NULL, bin = NULL, edge = NULL;
 *   RasterContourSet set = NULL;
 *
 *   if (Raster_LoadImage("part.png", &src) != RASTER_OK) { ... }
 *   Raster_ToGray(src, &bin);
 *   Raster_Threshold(bin, RASTER_THRESH_OTSU, 0, 255, &bin2);
 *   Raster_TraceContours(bin2, RASTER_TRACE_ALL, 10.0, 0.5, &set);
 *   Raster_KerfCompensate(set, 0.15, RASTER_KERF_OUTSIDE, &set2);
 *   Raster_OrderContours(set2, RASTER_ORDER_INNER_FIRST, &set3);
 *   // ... 交给刀路模块生成走刀 ...
 *
 *   Raster_DestroyContourSet(set3);
 *   Raster_DestroyImage(bin2);
 *   Raster_DestroyImage(src);
 * @endcode
 */

#include "RasterTypes.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* ========================================================================
     *                            版本 / 状态
     * ====================================================================== */

    /** @return packed 语义版本 (major<<16|minor<<8|patch)，应等于 RASTER_VERSION。 */
    RASTER_API uint32_t RASTER_CALL Raster_GetVersion(void);

    /** @return 运行期库的 ABI 主版本号，应等于 RASTER_ABI_VERSION。 */
    RASTER_API uint32_t RASTER_CALL Raster_GetAbiVersion(void);

    /** @return 形如 "1.0.0" 的版本串（静态存储期，调用方不得释放）。 */
    RASTER_API const char* RASTER_CALL Raster_GetVersionString(void);

    /** @return OpenCV 主版本号（用于排查跨版本行为差异），0 表示不可用。 */
    RASTER_API uint32_t RASTER_CALL Raster_GetOpenCvVersion(void);

    /**
     * @brief 状态码转可读文本。
     * @return 静态存储期的英文短描述，**永不为 NULL**；未知码返回 "Unknown status"。
     */
    RASTER_API const char* RASTER_CALL Raster_StatusText(int32_t status);

    /**
     * @brief 取最近一次失败的详细消息（线程局部）。
     * @param buffer      调用方缓冲区
     * @param bufferSize  缓冲区字节数
     * @return 写入的字节数（不含结尾 '\0'）；缓冲区不足时返回负值。
     */
    RASTER_API int RASTER_CALL Raster_GetLastErrorMessage(char* buffer, size_t bufferSize);

    /* ========================================================================
     *                            图像生命周期
     * ====================================================================== */

    /**
     * @brief 分配一张全零图像。
     * @param width   宽（像素，>0）
     * @param height  高（像素，>0）
     * @param format  RasterPixelFormat
     * @param out     输出句柄（成功时写入）
     */
    RASTER_API RasterStatus RASTER_CALL Raster_CreateImage(int32_t width, int32_t height,
                                                          int32_t format, RasterImage* out);

    /**
     * @brief 用调用方内存构造图像（**零拷贝**，深拷贝一份到库内以保证生命周期独立）。
     *
     * 深拷贝是刻意选择：库内图像的生命周期由句柄决定，调用方的缓冲区随时可能
     * 被释放或复用；让库持有指针会制造悬垂引用这类难以定位的缺陷。
     * 真正需要零拷贝的场景请用 Raster_GetImageView() 取借用视图自行处理。
     */
    RASTER_API RasterStatus RASTER_CALL Raster_CreateImageFromView(const RasterImageView* view,
                                                                   RasterImage* out);

    /** @brief 深拷贝图像。 */
    RASTER_API RasterStatus RASTER_CALL Raster_CloneImage(RasterImage src, RasterImage* out);

    /** @brief 查询图像元信息。 */
    RASTER_API RasterStatus RASTER_CALL Raster_GetImageInfo(RasterImage img, RasterImageInfo* out);

    /**
     * @brief 取得图像的**借用视图**（只读）。
     *
     * 视图中的 data 指向库内内存，**有效期到该句柄被销毁或被下一个写操作替换**。
     * 只读用途：直接把像素喂给渲染器 / 编码器，避免一次拷贝。
     * 需要改写请用 Raster_CopyImageTo()。
     */
    RASTER_API RasterStatus RASTER_CALL Raster_GetImageView(RasterImage img, RasterImageView* out);

    /**
     * @brief 把图像像素拷贝到调用方缓冲区。
     * @param dst       目标缓冲区
     * @param dstSize   目标缓冲区字节数
     * @param dstStride 目标每行字节数；0 = 紧凑排列
     * @return RASTER_E_BUFFER_TOO_SMALL 表示缓冲区不足。
     */
    RASTER_API RasterStatus RASTER_CALL Raster_CopyImageTo(RasterImage img, void* dst,
                                                           size_t dstSize, int32_t dstStride);

    /** @brief 释放图像句柄。传 NULL 是安全的空操作。 */
    RASTER_API void RASTER_CALL Raster_DestroyImage(RasterImage img);

    /* ========================================================================
     *                              文件读写
     * ====================================================================== */

    /**
     * @brief 从文件载入图像（按扩展名推断格式）。
     * @param path UTF-8 路径
     */
    RASTER_API RasterStatus RASTER_CALL Raster_LoadImage(const char* path, RasterImage* out);

    /**
     * @brief 保存图像（按扩展名推断格式）。
     * @param path UTF-8 路径
     */
    RASTER_API RasterStatus RASTER_CALL Raster_SaveImage(RasterImage img, const char* path);

    /**
     * @brief 以指定格式保存图像（扩展名与 format 不一致时以 format 为准）。
     * @param format RasterFileFormat
     * @param quality JPEG/WEBP 质量 1..100，其它格式忽略（0 表示使用默认值）
     */
    RASTER_API RasterStatus RASTER_CALL Raster_SaveImageAs(RasterImage img, const char* path,
                                                           int32_t format, int32_t quality);

    /* ========================================================================
     *                            通用预处理
     * ====================================================================== */

    /** @brief 转灰度（已是灰度时返回副本）。 */
    RASTER_API RasterStatus RASTER_CALL Raster_ToGray(RasterImage src, RasterImage* out);

    /** @brief 反色。 */
    RASTER_API RasterStatus RASTER_CALL Raster_Invert(RasterImage src, RasterImage* out);

    /**
     * @brief 亮度 / 对比度调整。
     * @param alpha 对比度增益（1.0 = 不变）
     * @param beta  亮度偏移（加在缩放之后）
     */
    RASTER_API RasterStatus RASTER_CALL Raster_AdjustBrightnessContrast(RasterImage src,
                                                                       double alpha, double beta,
                                                                       RasterImage* out);

    /** @brief 全局直方图均衡（仅灰度有效）。 */
    RASTER_API RasterStatus RASTER_CALL Raster_EqualizeHist(RasterImage src, RasterImage* out);

    /**
     * @brief 自适应直方图均衡 CLAHE。
     * @param clipLimit 对比度截断阈值（>0）
     * @param gridSize  网格大小（>=1）
     */
    RASTER_API RasterStatus RASTER_CALL Raster_Clahe(RasterImage src, double clipLimit,
                                                     int32_t gridSize, RasterImage* out);

    /**
     * @brief 去噪。
     * @param method   RasterDenoiseMethod
     * @param strength 强度/σ 参数，含义随 method 变化
     */
    RASTER_API RasterStatus RASTER_CALL Raster_Denoise(RasterImage src, int32_t method,
                                                       double strength, RasterImage* out);

    /**
     * @brief 锐化（反锐化掩模）。
     * @param amount 强度，0 = 关闭，1 = 适中
     */
    RASTER_API RasterStatus RASTER_CALL Raster_Sharpen(RasterImage src, double amount,
                                                       RasterImage* out);

    /**
     * @brief 阈值化。
     * @param method  RasterThresholdMethod
     * @param thresh  阈值（method=BINARY 时生效；ADAPTIVE 时为 blockSize）
     * @param maxValue 二值化后的最大值
     */
    RASTER_API RasterStatus RASTER_CALL Raster_Threshold(RasterImage src, int32_t method,
                                                         double thresh, double maxValue,
                                                         RasterImage* out);

    /**
     * @brief 形态学运算。
     * @param op         RasterMorphOp
     * @param kernelShape RasterKernelShape
     * @param ksize      核尺寸（奇数，>=1）
     */
    RASTER_API RasterStatus RASTER_CALL Raster_Morphology(RasterImage src, int32_t op,
                                                          int32_t kernelShape, int32_t ksize,
                                                          RasterImage* out);

    /**
     * @brief 缩放。
     * @param fx 横向比例（<=0 表示按 fy 等比）
     * @param fy 纵向比例
     * @param interpolation RasterInterpolation
     */
    RASTER_API RasterStatus RASTER_CALL Raster_Resize(RasterImage src, double fx, double fy,
                                                       int32_t interpolation, RasterImage* out);

    /**
     * @brief 旋转（保持画布尺寸，内容可能被裁掉）。
     * @param angleDeg   角度（度）
     * @param borderMode RasterBorderMode
     * @param borderValue 边界填充值（CONSTANT 时生效）
     */
    RASTER_API RasterStatus RASTER_CALL Raster_Rotate(RasterImage src, double angleDeg,
                                                      int32_t borderMode, double borderValue,
                                                      RasterImage* out);

    /** @brief 裁剪。超出图像范围的部分自动裁到边界。 */
    RASTER_API RasterStatus RASTER_CALL Raster_Crop(RasterImage src, int32_t x, int32_t y,
                                                     int32_t width, int32_t height,
                                                     RasterImage* out);

    /** @brief 翻转。@param axis RasterFlipAxis */
    RASTER_API RasterStatus RASTER_CALL Raster_Flip(RasterImage src, int32_t axis, RasterImage* out);

    /** @brief 倾斜校正：估计文字/图案主方向并旋正。 */
    RASTER_API RasterStatus RASTER_CALL Raster_Deskew(RasterImage src, RasterImage* out);

    /** @brief Canny 边缘检测。 */
    RASTER_API RasterStatus RASTER_CALL Raster_Canny(RasterImage src, double low, double high,
                                                     RasterImage* out);

    /**
     * @brief 平场校正：估计不均匀背景并扣除。
     *
     * 激光相机在暗场下常出现固定图案的照明不均，直接二值化会把背景照度当成
     * 图案。本算子用大核模糊估计背景，再做除法归一化。
     */
    RASTER_API RasterStatus RASTER_CALL Raster_FlatFieldCorrect(RasterImage src,
                                                                int32_t backgroundRadius,
                                                                RasterImage* out);

    /* ========================================================================
     *                          激光工艺专用处理
     * ====================================================================== */

    /**
     * @brief 抖动二值化：把灰度图转成 1 bit 雕刻图。
     * @param method RasterDitherMethod
     */
    RASTER_API RasterStatus RASTER_CALL Raster_Dither(RasterImage src, int32_t method,
                                                      RasterImage* out);

    /**
     * @brief 半调网屏：把连续灰度转成网点/线条屏，用于照片雕刻。
     * @param cellSize  网屏单元尺寸（像素，>=2）
     * @param angleDeg  网屏角度（度）
     * @param method    RasterHalftoneMethod
     */
    RASTER_API RasterStatus RASTER_CALL Raster_Halftone(RasterImage src, int32_t cellSize,
                                                        double angleDeg, int32_t method,
                                                        RasterImage* out);

    /**
     * @brief 去孤点：剔除面积小于 minArea 的连通域。
     *
     * 扫描件上的孤立噪点会在雕刻时变成一个个多余的烧穿点。
     * @param radius  连通域分析用的膨胀半径（像素，>=0）
     * @param minArea 面积阈值（像素²）
     */
    RASTER_API RasterStatus RASTER_CALL Raster_Despeckle(RasterImage src, int32_t radius,
                                                        int32_t minArea, RasterImage* out);

    /**
     * @brief 灰度 → 16 位高度图（2.5D 起伏雕刻用）。
     * @param minHeight 输出最小高度（0..1）
     * @param maxHeight 输出最大高度（0..1）
     */
    RASTER_API RasterStatus RASTER_CALL Raster_HeightMapFromGray(RasterImage src,
                                                                double minHeight,
                                                                double maxHeight,
                                                                RasterImage* out);

    /**
     * @brief 打包成 1 bit 每像素的位图（激光控制器常用格式）。
     *
     * 每行按位打包，**每行独立补齐到字节边界**（不做整图比特流对齐），
     * 这样任意行宽都能被逐行解释，代价是行尾有最多 7 位填充。
     * @param buffer     目标缓冲区
     * @param bufferSize 缓冲区字节数
     * @param written    实际写入字节数（可为 NULL）
     * @param strideBytes 每行字节数（可为 NULL）
     * @return RASTER_E_BUFFER_TOO_SMALL 表示缓冲区不足。
     */
    RASTER_API RasterStatus RASTER_CALL Raster_PackToOneBit(RasterImage src, uint8_t* buffer,
                                                           size_t bufferSize, size_t* written,
                                                           int32_t* strideBytes);

    /* ========================================================================
     *                            轮廓 / 切割路径
     * ====================================================================== */

    /**
     * @brief 提取轮廓（切割路径候选）。
     * @param mode          RasterTraceMode
     * @param minArea       最小面积（像素²），小于该值的轮廓被丢弃
     * @param approxEpsilon 多边形逼近精度（像素，<=0 表示不逼近，保留原始轮廓点）
     */
    RASTER_API RasterStatus RASTER_CALL Raster_TraceContours(RasterImage src, int32_t mode,
                                                             double minArea, double approxEpsilon,
                                                             RasterContourSet* out);

    /**
     * @brief 割缝（kerf）补偿：对轮廓做等距偏移。
     *
     * 激光切割会烧掉一条宽度为 kerf 的材料。若按原轮廓走刀，零件实际尺寸会
     * 小约 kerf/2；本算子把轮廓外扩 kerf/2（OUTSIDE）使成品尺寸达标。
     * @param kerfWidth 割缝宽度（像素，>=0）
     * @param side      RasterKerfSide
     */
    RASTER_API RasterStatus RASTER_CALL Raster_KerfCompensate(RasterContourSet src,
                                                             double kerfWidth, int32_t side,
                                                             RasterContourSet* out);

    /** @brief 轮廓排序（走刀顺序）。 */
    RASTER_API RasterStatus RASTER_CALL Raster_OrderContours(RasterContourSet src, int32_t mode,
                                                            RasterContourSet* out);

    /** @brief 轮廓集合的轮廓数量。 */
    RASTER_API RasterStatus RASTER_CALL Raster_ContourSetCount(RasterContourSet set, int32_t* out);

    /** @brief 取第 index 条轮廓的顶点（借用视图）。 */
    RASTER_API RasterStatus RASTER_CALL Raster_ContourSetGet(RasterContourSet set, int32_t index,
                                                            RasterContour* out);

    /** @brief 取第 index 条轮廓的几何摘要。 */
    RASTER_API RasterStatus RASTER_CALL Raster_ContourSetGetInfo(RasterContourSet set, int32_t index,
                                                                 RasterContourInfo* out);

    /** @brief 释放轮廓集合。传 NULL 是安全的空操作。 */
    RASTER_API void RASTER_CALL Raster_DestroyContourSet(RasterContourSet set);

    /* ========================================================================
     *                        码识别（打标防错 / 追溯）
     * ====================================================================== */

    /**
     * @brief 识别图中的二维码与一维码。
     *
     * 激光打标场景的典型用法：打完追溯码后拍一张照，把解出来的内容与工单里的
     * 料号比对，防止打错料号还流到下一道工序。
     *
     * @param src 待识别图像（会先转灰度）
     * @param out 输出码集合
     */
    RASTER_API RasterStatus RASTER_CALL Raster_DetectCodes(RasterImage src, RasterCodeSet* out);

    /**
     * @brief 校验图中的码内容是否与期望一致。
     * @param expected 期望的码文本（UTF-8）
     * @param out      1 = 找到且内容一致；0 = 找到但内容不一致
     * @return RASTER_E_NOT_FOUND 表示图里根本没有可解码的码；
     *         RASTER_E_INVALID_ARG 表示 expected 为空。
     */
    RASTER_API RasterStatus RASTER_CALL Raster_VerifyCode(RasterImage src, const char* expected,
                                                         int32_t* out);

    /** @brief 码集合数量。 */
    RASTER_API RasterStatus RASTER_CALL Raster_CodeSetCount(RasterCodeSet set, int32_t* out);

    /** @brief 取第 index 个码。 */
    RASTER_API RasterStatus RASTER_CALL Raster_CodeSetGet(RasterCodeSet set, int32_t index,
                                                         RasterCode* out);

    /** @brief 释放码集合。传 NULL 是安全的空操作。 */
    RASTER_API void RASTER_CALL Raster_DestroyCodeSet(RasterCodeSet set);

    /* ========================================================================
     *                              统计 / 检测
     * ====================================================================== */

    /** @brief 灰度统计（min/max/mean/std/墨量占比）。 */
    RASTER_API RasterStatus RASTER_CALL Raster_ComputeStats(RasterImage src, RasterStats* out);

    /** @brief 灰度直方图（256 桶，桶数据由调用方提供）。 */
    RASTER_API RasterStatus RASTER_CALL Raster_Histogram(RasterImage src, RasterHistogram* out);

    /**
     * @brief 缺陷检测（污点 / 划痕 / 孔洞）。
     * @param sensitivity 灵敏度（0..1，越大越敏感）
     * @param minArea     最小面积（像素²）
     */
    RASTER_API RasterStatus RASTER_CALL Raster_DetectDefects(RasterImage src, double sensitivity,
                                                            double minArea, RasterDefectSet* out);

    /** @brief 缺陷集合数量。 */
    RASTER_API RasterStatus RASTER_CALL Raster_DefectSetCount(RasterDefectSet set, int32_t* out);

    /** @brief 取第 index 个缺陷。 */
    RASTER_API RasterStatus RASTER_CALL Raster_DefectSetGet(RasterDefectSet set, int32_t index,
                                                           RasterDefect* out);

    /** @brief 释放缺陷集合。传 NULL 是安全的空操作。 */
    RASTER_API void RASTER_CALL Raster_DestroyDefectSet(RasterDefectSet set);

    /** @brief 两图比对（标定重复性 / 来料一致性）。 */
    RASTER_API RasterStatus RASTER_CALL Raster_CompareImages(RasterImage a, RasterImage b,
                                                             RasterCompareResult* out);

    /* ========================================================================
     *                              处理管线
     * ====================================================================== */

    /** @brief 创建管线。 */
    RASTER_API RasterStatus RASTER_CALL Raster_CreatePipeline(RasterPipeline* out);

    /**
     * @brief 追加一个算子。
     * @param params     RasterOpParams（库内深拷贝其内容）
     * @param paramsSize 必须是 sizeof(RasterOpParams)
     */
    RASTER_API RasterStatus RASTER_CALL Raster_PipelineAdd(RasterPipeline pipeline, int32_t opId,
                                                           const void* params, size_t paramsSize);

    /** @brief 清空管线中的所有算子。 */
    RASTER_API RasterStatus RASTER_CALL Raster_PipelineClear(RasterPipeline pipeline);

    /** @brief 管线算子数量。 */
    RASTER_API RasterStatus RASTER_CALL Raster_PipelineCount(RasterPipeline pipeline, int32_t* out);

    /**
     * @brief 执行管线：src 依次经过各算子，产出最终图像。
     *
     * 管线内部不修改 src；中途结果由库持有，最终结果通过 out 交出。
     */
    RASTER_API RasterStatus RASTER_CALL Raster_PipelineExecute(RasterPipeline pipeline,
                                                               RasterImage src, RasterImage* out);

    /** @brief 释放管线。传 NULL 是安全的空操作。 */
    RASTER_API void RASTER_CALL Raster_DestroyPipeline(RasterPipeline pipeline);

#ifdef __cplusplus
}
#endif

#endif /* RASTER_API_H */
