#ifndef RASTER_TYPES_H
#define RASTER_TYPES_H

/**
 * @file RasterTypes.h
 * @brief Raster 模块的**纯 POD** 类型定义（跨 DLL 边界的数据契约）。
 *
 * ============================================================================
 * 契约（硬约束，违反即编译期/配置期报错）
 * ============================================================================
 *
 *  1. 本头**不含任何 C++ 构造**：没有 std::string / std::vector / std::array /
 *     模板 / 引用 / 虚函数 / 异常。
 *  2. 所有结构体只含定长算术类型与指针；跨模块传递的字符串一律用
 *     `(const char*)` 或 `(char* buffer, size_t bufferSize)`。
 *  3. 所有"入参结构体"的**第一个字段固定为 `structSize`**。库端用 structSize
 *     判断调用方编译时的结构体版本，从而在尾部追加字段时保持二进制兼容；
 *     识别不出时返回 RASTER_E_STRUCT_SIZE，而不是越界读。
 *  4. 枚举一律显式指定底层类型为 int32_t（与 Docs/01-当前架构/C-ABI风格约定.md 一致）。
 *  5. 布尔语义用 int32_t（0/1），不使用 C++ bool / _Bool，避免不同编译器
 *     对 bool 大小与 ABI 的假设差异。
 *
 * ============================================================================
 * 坐标系与单位约定
 * ============================================================================
 *
 *  - 像素坐标原点在**左上角**，x 向右、y 向下（OpenCV 原生约定）。
 *    与 CAD 几何坐标（原点左下、y 向上）不一致，转换责任在调用方。
 *  - 长度单位一律为**像素**；若调用方需要毫米，请自行乘以像素当量
 *    (mm per pixel)，本模块不做单位换算，避免"看起来像但其实是像素"的隐患。
 *  - 角度单位一律为**度**，逆时针为正（图像坐标系下即为顺时针视觉方向，
 *    调用方按需取负）。
 */

#include "RasterExport.h"

#include <stddef.h>
#include <stdint.h>

/* ============================================================================
 *                                 版本
 * ========================================================================== */

/** ABI 主版本。破坏性变更 +1；仅新增接口/字段则不变。 */
#define RASTER_ABI_VERSION 1

#define RASTER_VERSION_MAJOR 1
#define RASTER_VERSION_MINOR 0
#define RASTER_VERSION_PATCH 0

/** packed 语义版本：(major<<16)|(minor<<8)|patch，与仓库 C-ABI 约定一致。 */
#define RASTER_MAKE_VERSION(major, minor, patch) \
    (((uint32_t)(major) << 16) | ((uint32_t)(minor) << 8) | (uint32_t)(patch))

#define RASTER_VERSION                                                   \
    RASTER_MAKE_VERSION(RASTER_VERSION_MAJOR, RASTER_VERSION_MINOR,      \
                         RASTER_VERSION_PATCH)

/** 把数字宏拼成字符串宏，用于生成版本串。 */
#define RASTER_STRINGIFY_IMPL(x) #x
#define RASTER_STRINGIFY(x) RASTER_STRINGIFY_IMPL(x)

/**
 * 人类可读版本串，例如 "1.0.0"。
 *
 * 由上面三个数字宏推导，而不是另写一个字面量：
 * 这样"宏说 1.0.0、字符串说 1.0.1"这种漂移在编译期就不可能发生。
 */
#define RASTER_VERSION_STRING                              \
    RASTER_STRINGIFY(RASTER_VERSION_MAJOR)                 \
    "." RASTER_STRINGIFY(RASTER_VERSION_MINOR)             \
    "." RASTER_STRINGIFY(RASTER_VERSION_PATCH)

/** 跨模块传递的缓冲区约定：调用方提供的指针在调用期间有效，库不接管所有权。 */
#define RASTER_MAX_PATH 1024

#ifdef __cplusplus
extern "C"
{
#endif

    /* ========================================================================
     *                               状态码
     * ====================================================================== */

    /**
     * @brief 模块统一状态码。成功值恒为 0（RASTER_OK）。
     *
     * 约定：**不使用异常**跨 DLL 边界。所有可能失败的调用返回本枚举，
     * 详细原因通过 Raster_GetLastErrorMessage() 取回。
     */
    typedef enum RasterStatus
    {
        RASTER_OK = 0,                /**< 成功 */
        RASTER_E_INVALID_ARG = -1,    /**< 空指针 / 非法数值 / 参数越界 */
        RASTER_E_OUT_OF_MEMORY = -2,  /**< 内存分配失败 */
        RASTER_E_IO = -3,             /**< 文件读写失败（路径不存在、格式不支持、磁盘错误） */
        RASTER_E_UNSUPPORTED = -4,    /**< 该功能在当前构建下不可用（缺 OpenCV 模块等） */
        RASTER_E_STRUCT_SIZE = -5,    /**< structSize 无法识别：调用方与库 ABI 不匹配 */
        RASTER_E_NOT_FOUND = -6,      /**< 索引越界 / 元素不存在 */
        RASTER_E_BUFFER_TOO_SMALL = -7, /**< 调用方缓冲区不足 */
        RASTER_E_EMPTY = -8,          /**< 输入为空（零尺寸图像、空轮廓集） */
        RASTER_E_INTERNAL = -99       /**< 内部一致性错误，按 bug 处理 */
    } RasterStatus;

    /* ========================================================================
     *                             像素格式
     * ====================================================================== */

    /** @brief 像素格式。值与 OpenCV 通道数一致，便于内部零拷贝转换。 */
    typedef enum RasterPixelFormat
    {
        RASTER_FORMAT_UNKNOWN = -1,
        RASTER_FORMAT_GRAY8 = 0,  /**< 单通道 8 位灰度 */
        RASTER_FORMAT_BGR8 = 1,   /**< 3 通道 8 位（BGR，非 RGB！） */
        RASTER_FORMAT_BGRA8 = 2,  /**< 4 通道 8 位 */
        RASTER_FORMAT_GRAY16 = 3  /**< 单通道 16 位（高度图 / 深度图） */
    } RasterPixelFormat;

    /* ========================================================================
     *                              图像
     * ====================================================================== */

    /**
     * @brief 不透明图像句柄。库内部持有图像数据，调用方只持有指针。
     *
     * 句柄**不是线程安全的**：同一句柄同一时刻只能被一个线程使用。
     * 确需并发时，为每个线程各自 Raster_CloneImage() 一份。
     */
    typedef struct RasterImageHandle* RasterImage;

    /** @brief 图像元信息（纯输出结构）。 */
    typedef struct RasterImageInfo
    {
        int32_t structSize;   /**< 入参：置为 sizeof(RasterImageInfo) */
        int32_t width;
        int32_t height;
        int32_t channels;
        int32_t format;       /**< RasterPixelFormat */
        size_t  dataSize;     /**< 像素数据字节数（不含行对齐填充） */
    } RasterImageInfo;

    /**
     * @brief 图像视图：**零拷贝**地引用调用方自己的内存。
     *
     * 用于两种方向：
     *   - 入参：把外部内存（QImage 缓冲、相机帧、文件映射）喂给库；
     *   - 出参：库把内部图像的借用视图交出来（配合 Raster_CopyImageTo 更安全）。
     *
     * 视图**不拥有** data 指针；调用方负责其生命周期。
     */
    typedef struct RasterImageView
    {
        int32_t     structSize; /**< 入参：置为 sizeof(RasterImageView) */
        int32_t     width;
        int32_t     height;
        int32_t     channels;
        int32_t     format;    /**< RasterPixelFormat */
        const void* data;      /**< 像素首地址 */
        int32_t     stride;    /**< 每行字节数；0 表示按 width*channels 紧凑排列 */
    } RasterImageView;

    /* ========================================================================
     *                            轮廓 / 路径
     * ====================================================================== */

    /**
     * @brief 轮廓集合句柄。
     *
     * 激光切割场景下，一条闭合轮廓 = 一条切割路径。集合内部保持"外环 / 孔洞"
     * 的拓扑关系（isHole 标志），调用方可据此决定走刀顺序。
     */
    typedef struct RasterContourHandle* RasterContourSet;

    /** @brief 单条轮廓的几何摘要（纯输出结构）。 */
    typedef struct RasterContourInfo
    {
        int32_t structSize; /**< 入参：置为 sizeof(RasterContourInfo) */
        int32_t pointCount;  /**< 顶点数 */
        int32_t isHole;      /**< 1 = 孔洞（内轮廓），0 = 外轮廓 */
        int32_t reserved;
        double  area;        /**< 有符号面积，像素²；外轮廓为正、孔洞为负 */
        double  perimeter;   /**< 周长，像素 */
        double  minX;
        double  minY;
        double  maxX;
        double  maxY;
    } RasterContourInfo;

    /**
     * @brief 单条轮廓的顶点数据（借用视图，生命周期与集合句柄一致）。
     *
     * points 为 x,y 交替排列的 double，单位像素，坐标系同图像。
     */
    typedef struct RasterContour
    {
        int32_t        structSize; /**< 入参：置为 sizeof(RasterContour) */
        int32_t        pointCount;
        int32_t        isHole;
        int32_t        reserved;
        const double* points;     /**< pointCount * 2 个 double */
    } RasterContour;

    /* ========================================================================
     *                          码识别（打标防错 / 追溯）
     * ====================================================================== */

    /** @brief 码的类型。 */
    typedef enum RasterCodeKind
    {
        RASTER_CODE_QR = 0,      /**< 二维码 */
        RASTER_CODE_BARCODE = 1, /**< 一维码 */
        RASTER_CODE_UNKNOWN = 2  /**< 检测到码形但无法解码 */
    } RasterCodeKind;

    /** @brief 码集合句柄。 */
    typedef struct RasterCodeHandle* RasterCodeSet;

    /**
     * @brief 单个码的识别结果。
     *
     * text 是**借用指针**：指向集合内部持有的字符串，
     * 有效期到集合被销毁。
     */
    typedef struct RasterCode
    {
        int32_t    structSize;
        int32_t    kind;       /**< RasterCodeKind */
        int32_t    x;          /**< 包围盒左上角 x（像素） */
        int32_t    y;          /**< 包围盒左上角 y（像素） */
        int32_t    width;      /**< 包围盒宽（像素） */
        int32_t    height;     /**< 包围盒高（像素） */
        int32_t    reserved;
        double     confidence; /**< 0..1，解码置信度；未知码为 0 */
        const char* text;      /**< 解码文本（借用指针，未能解码时为空串） */
    } RasterCode;

    /* ========================================================================
     *                              统计 / 检测
     * ====================================================================== */

    /** @brief 灰度统计摘要。 */
    typedef struct RasterStats
    {
        int32_t structSize;
        int32_t width;
        int32_t height;
        int32_t reserved;
        double  min;       /**< 最小灰度值 */
        double  max;       /**< 最大灰度值 */
        double  mean;      /**< 平均灰度值 */
        double  stdDev;    /**< 标准差 */
        double  inkRatio;  /**< 暗像素占比（灰度 < 128 的像素比例），0..1 */
    } RasterStats;

    /**
     * @brief 直方图。
     *
     * bins 由调用方分配；输入时 binCount = 容量，输出时 binCount = 实际写入数。
     * 固定 256 个桶：8 位灰度直接统计；16 位图会先线性压到 8 位再统计。
     */
    typedef struct RasterHistogram
    {
        int32_t  structSize;
        int32_t  binCount; /**< 入：容量；出：实际写入的桶数 */
        int32_t  reserved;
        uint32_t* bins;
    } RasterHistogram;

    /** @brief 缺陷类别。 */
    typedef enum RasterDefectKind
    {
        RASTER_DEFECT_SPOT = 0,   /**< 污点 / 黑点（形态学 top-hat 检出） */
        RASTER_DEFECT_SCRATCH = 1, /**< 划痕（bottom-hat 检出的细长暗痕） */
        RASTER_DEFECT_HOLE = 2     /**< 孔洞 / 缺失（亮斑） */
    } RasterDefectKind;

    /** @brief 单个缺陷的定位与严重度。 */
    typedef struct RasterDefect
    {
        int32_t structSize;
        int32_t kind;     /**< RasterDefectKind */
        int32_t x;        /**< 包围盒左上角 x（像素） */
        int32_t y;        /**< 包围盒左上角 y（像素） */
        int32_t width;    /**< 包围盒宽（像素） */
        int32_t height;   /**< 包围盒高（像素） */
        double  area;     /**< 缺陷面积（像素²） */
        double  severity; /**< 严重度 0..1，越大越严重 */
    } RasterDefect;

    /** @brief 缺陷集合句柄。 */
    typedef struct RasterDefectHandle* RasterDefectSet;

    /** @brief 两图比对结果（用于标定重复性 / 来料一致性检查）。 */
    typedef struct RasterCompareResult
    {
        int32_t structSize;
        int32_t differingPixels; /**< 差异像素数 */
        int32_t totalPixels;     /**< 总像素数 */
        int32_t reserved;
        double  meanAbsDiff;     /**< 平均绝对差 */
        double  maxAbsDiff;      /**< 最大绝对差 */
        double  rmse;            /**< 均方根误差 */
        double  similarity;      /**< 相似度 0..1（1 - 归一化 RMSE） */
    } RasterCompareResult;

    /* ========================================================================
     *                              处理管线
     * ====================================================================== */

    /** @brief 管线句柄：把一串处理步骤固化为可复用的工艺流程。 */
    typedef struct RasterPipelineHandle* RasterPipeline;

    /**
     * @brief 管线算子的通用参数块。
     *
     * 为什么用"通用块"而不是"每个算子一个结构体"：
     *   - 管线需要可序列化（存配方、下发给设备），通用块可以直接按定长二进制落盘；
     *   - 每算子一个结构体会让管线 ABI 随算子数量线性膨胀。
     * 代价是语义靠文档约束，因此下表逐算子给出字段含义，**新增字段只能追加**。
     *
     * | opId                        | 字段含义                                        |
     * |-----------------------------|-------------------------------------------------|
     * | RASTER_OP_TO_GRAY           | 无                                              |
     * | RASTER_OP_INVERT            | 无                                              |
     * | RASTER_OP_ADJUST_CONTRAST   | f0=alpha(对比度) f1=beta(亮度)                  |
     * | RASTER_OP_THRESHOLD         | i0=method f0=thresh f1=maxValue                 |
     * | RASTER_OP_OTSU              | f0=maxValue                                     |
     * | RASTER_OP_CLAHE             | f0=clipLimit i0=gridSize                        |
     * | RASTER_OP_EQUALIZE          | 无                                              |
     * | RASTER_OP_DENOISE           | i0=method f0=strength f1=sigmaSpace i0b=ksize   |
     * | RASTER_OP_SHARPEN           | f0=amount                                       |
     * | RASTER_OP_MORPHOLOGY        | i0=op i1=kernelShape i2=ksize                   |
     * | RASTER_OP_RESIZE            | f0=fx f1=fy i0=interpolation                    |
     * | RASTER_OP_ROTATE            | f0=angleDeg i0=borderMode f1=borderValue        |
     * | RASTER_OP_CROP              | i0=x i1=y i2=w i3=h                             |
     * | RASTER_OP_FLIP              | i0=axis                                         |
     * | RASTER_OP_DITHER            | i0=method                                       |
     * | RASTER_OP_HALFTONE          | i0=cellSize f0=angleDeg i1=method              |
     * | RASTER_OP_DESPECKLE         | i0=radius i1=minArea                            |
     * | RASTER_OP_CANNY             | f0=low f1=high                                  |
     * | RASTER_OP_DESKEW            | 无                                              |
     * | RASTER_OP_HEIGHT_MAP        | f0=minHeight f1=maxHeight                       |
     * | RASTER_OP_FLAT_FIELD        | f0=backgroundRadius                             |
     * | RASTER_OP_ERODE_BORDER      | i0=borderPixels                                 |
     */
    typedef struct RasterOpParams
    {
        int32_t structSize; /**< 入参：置为 sizeof(RasterOpParams) */
        int32_t opId;       /**< RasterOpId */
        int32_t reserved;
        double  f0;
        double  f1;
        double  f2;
        double  f3;
        int32_t i0;
        int32_t i1;
        int32_t i2;
        int32_t i3;
    } RasterOpParams;

    /** @brief 管线算子编号。 */
    typedef enum RasterOpId
    {
        RASTER_OP_NONE = 0,
        RASTER_OP_TO_GRAY = 1,
        RASTER_OP_INVERT = 2,
        RASTER_OP_ADJUST_CONTRAST = 3,
        RASTER_OP_THRESHOLD = 4,
        RASTER_OP_OTSU = 5,
        RASTER_OP_CLAHE = 6,
        RASTER_OP_EQUALIZE = 7,
        RASTER_OP_DENOISE = 8,
        RASTER_OP_SHARPEN = 9,
        RASTER_OP_MORPHOLOGY = 10,
        RASTER_OP_RESIZE = 11,
        RASTER_OP_ROTATE = 12,
        RASTER_OP_CROP = 13,
        RASTER_OP_FLIP = 14,
        RASTER_OP_DITHER = 15,
        RASTER_OP_HALFTONE = 16,
        RASTER_OP_DESPECKLE = 17,
        RASTER_OP_CANNY = 18,
        RASTER_OP_DESKEW = 19,
        RASTER_OP_HEIGHT_MAP = 20,
        RASTER_OP_FLAT_FIELD = 21,
        RASTER_OP_ERODE_BORDER = 22
    } RasterOpId;

    /* ========================================================================
     *                            枚举：处理参数
     * ====================================================================== */

    /** @brief 阈值化方法。 */
    typedef enum RasterThresholdMethod
    {
        RASTER_THRESH_BINARY = 0,   /**< 固定阈值 f0，f1=maxValue */
        RASTER_THRESH_OTSU = 1,     /**< 全局 Otsu 自动阈值 */
        RASTER_THRESH_ADAPTIVE = 2, /**< 局部自适应：f0=blockSize(奇数) f1=C */
        RASTER_THRESH_TRIANGLE = 3, /**< 三角法（灰度直方图单峰时更稳） */
        RASTER_THRESH_TRUNC = 4,    /**< 截断为 0 */
        RASTER_THRESH_TOZERO = 5    /**< 低于阈值置 0 */
    } RasterThresholdMethod;

    /** @brief 去噪方法。 */
    typedef enum RasterDenoiseMethod
    {
        RASTER_DENOISE_FAST_NLMEANS = 0, /**< f0=h（去噪强度） */
        RASTER_DENOISE_BILATERAL = 1,     /**< f0=sigmaColor f1=sigmaSpace i0=d */
        RASTER_DENOISE_MEDIAN = 2,        /**< i0=ksize（奇数） */
        RASTER_DENOISE_GAUSSIAN = 3       /**< f0=sigmaX f1=sigmaY */
    } RasterDenoiseMethod;

    /** @brief 抖动（半色调）方法。用于把灰度图变成 1 bit 雕刻图。 */
    typedef enum RasterDitherMethod
    {
        RASTER_DITHER_NONE = 0,           /**< 纯阈值，无抖动 */
        RASTER_DITHER_FLOYD_STEINBERG = 1,/**< 经典误差扩散 */
        RASTER_DITHER_ATKINSON = 2,       /**< 1/3 扩散，视觉噪声更小 */
        RASTER_DITHER_BAYER_8X8 = 3,      /**< 有序抖动（8x8  Bayer） */
        RASTER_DITHER_CLUSTERED_DOT = 4   /**< 聚簇网点（印刷风格） */
    } RasterDitherMethod;

    /** @brief 网屏（半调）方式。 */
    typedef enum RasterHalftoneMethod
    {
        RASTER_HALFTONE_DOT = 0,   /**< 聚簇点阵 */
        RASTER_HALFTONE_LINE = 1,  /**< 线条网屏 */
        RASTER_HALFTONE_CROSS = 2  /**< 交叉网屏 */
    } RasterHalftoneMethod;

    /** @brief 轮廓提取范围。 */
    typedef enum RasterTraceMode
    {
        RASTER_TRACE_EXTERNAL = 0, /**< 只取最外层轮廓 */
        RASTER_TRACE_ALL = 1,      /**< 全部轮廓（含孔洞） */
        RASTER_TRACE_HOLES = 2     /**< 只取孔洞轮廓 */
    } RasterTraceMode;

    /** @brief 走刀顺序策略。 */
    typedef enum RasterContourOrder
    {
        RASTER_ORDER_INPUT = 0,        /**< 保持提取顺序 */
        RASTER_ORDER_INNER_FIRST = 1,  /**< 先内后外：避免切完外框后零件掉落 */
        RASTER_ORDER_OUTER_FIRST = 2,  /**< 先外后内 */
        RASTER_ORDER_BY_AREA_DESC = 3, /**< 面积从大到小 */
        RASTER_ORDER_BY_AREA_ASC = 4,  /**< 面积从小到大 */
        RASTER_ORDER_BY_LENGTH_DESC = 5 /**< 周长从大到小 */
    } RasterContourOrder;

    /** @brief 割缝补偿方向。 */
    typedef enum RasterKerfSide
    {
        RASTER_KERF_OUTSIDE = 0, /**< 外扩：切缝落在材料上，零件尺寸偏小 */
        RASTER_KERF_INSIDE = 1,  /**< 内缩：零件尺寸偏大 */
        RASTER_KERF_CENTER = 2   /**< 居中（不加补偿） */
    } RasterKerfSide;

    /** @brief 翻转轴。 */
    typedef enum RasterFlipAxis
    {
        RASTER_FLIP_HORIZONTAL = 0,
        RASTER_FLIP_VERTICAL = 1,
        RASTER_FLIP_BOTH = 2
    } RasterFlipAxis;

    /** @brief 形态学算子。 */
    typedef enum RasterMorphOp
    {
        RASTER_MORPH_ERODE = 0,
        RASTER_MORPH_DILATE = 1,
        RASTER_MORPH_OPEN = 2,
        RASTER_MORPH_CLOSE = 3,
        RASTER_MORPH_GRADIENT = 4,
        RASTER_MORPH_TOPHAT = 5,   /**< 提取亮于背景的细结构（污点/划痕检测） */
        RASTER_MORPH_BLACKHAT = 6 /**< 提取暗于背景的细结构 */
    } RasterMorphOp;

    /** @brief 核形状。 */
    typedef enum RasterKernelShape
    {
        RASTER_KERNEL_RECT = 0,
        RASTER_KERNEL_ELLIPSE = 1,
        RASTER_KERNEL_CROSS = 2
    } RasterKernelShape;

    /** @brief 边界填充方式。 */
    typedef enum RasterBorderMode
    {
        RASTER_BORDER_CONSTANT = 0,
        RASTER_BORDER_REPLICATE = 1,
        RASTER_BORDER_REFLECT = 2,
        RASTER_BORDER_WRAP = 3
    } RasterBorderMode;

    /** @brief 插值方式。 */
    typedef enum RasterInterpolation
    {
        RASTER_INTERP_NEAREST = 0,
        RASTER_INTERP_LINEAR = 1,
        RASTER_INTERP_CUBIC = 2,
        RASTER_INTERP_AREA = 3,   /**< 缩小时质量最好 */
        RASTER_INTERP_LANCZOS4 = 4
    } RasterInterpolation;

    /** @brief 文件格式。 */
    typedef enum RasterFileFormat
    {
        RASTER_FILE_PNG = 0,
        RASTER_FILE_BMP = 1,
        RASTER_FILE_TIFF = 2,
        RASTER_FILE_JPEG = 3,
        RASTER_FILE_WEBP = 4
    } RasterFileFormat;

#ifdef __cplusplus
}
#endif

#endif /* RASTER_TYPES_H */
