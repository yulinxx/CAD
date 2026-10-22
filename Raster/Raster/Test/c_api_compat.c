/*
 * c_api_compat.c
 *
 * 纯 C 消费者示例（同时也是编译期契约测试）。
 *
 * 存在意义：证明 Include/Raster 下的头文件**确实是纯 C 头**。
 * 本文件以 C99 编译，一旦头里出现任何 C++ 构造（类、模板、引用、
 * 重载、std::string ...），编译立刻失败。
 *
 * 第三方用 ctypes / cgo / Delphi / C# P-Invoke 消费 Raster 时，
 * 遇到的就是完全相同的约束。
 */

#include <Raster/RasterApi.h>

#include <stdio.h>

static int fail(const char* what)
{
    fprintf(stderr, "FAIL: %s\n", what);
    return 1;
}

int main(void)
{
    char message[256];
    RasterImage image = NULL;
    RasterImage gray = NULL;
    RasterStats stats;

    /* --- 版本与状态 --- */
    if (Raster_GetVersion() == 0u) {
        return fail("Raster_GetVersion returned 0");
    }
    if (Raster_GetVersionString() == NULL || Raster_GetVersionString()[0] == '\0') {
        return fail("Raster_GetVersionString returned an empty string");
    }
    if (Raster_StatusText(RASTER_OK) == NULL) {
        return fail("Raster_StatusText(RASTER_OK) returned NULL");
    }
    if (Raster_StatusText(-12345) == NULL) {
        return fail("Raster_StatusText(unknown) returned NULL");
    }

    /* --- 错误信息缓冲 --- */
    (void)Raster_GetLastErrorMessage(message, sizeof(message));

    /* --- 图像生命周期 --- */
    if (Raster_CreateImage(64, 48, RASTER_FORMAT_BGR8, &image) != RASTER_OK) {
        return fail("Raster_CreateImage failed");
    }
    if (Raster_ToGray(image, &gray) != RASTER_OK) {
        return fail("Raster_ToGray failed");
    }

    memset(&stats, 0, sizeof(stats));
    stats.structSize = (int32_t)sizeof(stats);
    if (Raster_ComputeStats(gray, &stats) != RASTER_OK) {
        return fail("Raster_ComputeStats failed");
    }
    if (stats.width != 64 || stats.height != 48) {
        return fail("unexpected image dimensions");
    }

    Raster_DestroyImage(gray);
    Raster_DestroyImage(image);

    /* 空句柄必须安全可释放 */
    Raster_DestroyImage(NULL);
    Raster_DestroyContourSet(NULL);
    Raster_DestroyDefectSet(NULL);
    Raster_DestroyPipeline(NULL);

    printf("Raster C API compatibility check passed (OpenCV %u)\n", Raster_GetOpenCvVersion());
    return 0;
}
