/*
 * Example_CApi.c
 *
 * 纯 C 消费示例：第三方用 C / ctypes / cgo / P-Invoke 时就是这个写法。
 *
 * 这里刻意不用 C++ 门面（RasterCpp.h），直接用 C ABI：
 *   - 所有类型都是 POD；
 *   - 句柄必须显式释放；
 *   - 失败时返回状态码，详细信息取自 Raster_GetLastErrorMessage()。
 */

#include <Raster/RasterApi.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static void printLastError(const char* where)
{
    char message[512];
    const int written = Raster_GetLastErrorMessage(message, sizeof(message));
    if (written > 0) {
        fprintf(stderr, "%s: %s\n", where, message);
    } else {
        fprintf(stderr, "%s: (no detail)\n", where);
    }
}

int main(int argc, char** argv)
{
    const char* input = (argc > 1) ? argv[1] : "input.png";
    RasterImage source = NULL;
    RasterImage gray = NULL;
    RasterImage binary = NULL;
    RasterImageInfo info;
    RasterStats stats;
    RasterStatus status;

    printf("Raster %s (ABI %u, OpenCV %u)\n", Raster_GetVersionString(), Raster_GetAbiVersion(),
           Raster_GetOpenCvVersion());

    status = Raster_LoadImage(input, &source);
    if (status != RASTER_OK) {
        fprintf(stderr, "load failed: %s\n", Raster_StatusText((int32_t)status));
        printLastError("Raster_LoadImage");
        return 1;
    }

    status = Raster_ToGray(source, &gray);
    if (status != RASTER_OK) {
        printLastError("Raster_ToGray");
        Raster_DestroyImage(source);
        return 1;
    }

    status = Raster_Threshold(gray, RASTER_THRESH_OTSU, 0.0, 255.0, &binary);
    if (status != RASTER_OK) {
        printLastError("Raster_Threshold");
        Raster_DestroyImage(gray);
        Raster_DestroyImage(source);
        return 1;
    }

    memset(&info, 0, sizeof(info));
    info.structSize = (int32_t)sizeof(info);
    Raster_GetImageInfo(binary, &info);
    printf("binary image: %dx%d ch=%d\n", (int)info.width, (int)info.height, (int)info.channels);

    memset(&stats, 0, sizeof(stats));
    stats.structSize = (int32_t)sizeof(stats);
    Raster_ComputeStats(binary, &stats);
    printf("ink ratio = %.4f\n", stats.inkRatio);

    Raster_SaveImage(binary, "output_c_api.png");

    /* 显式释放：C ABI 没有 RAII，忘了就是泄漏。 */
    Raster_DestroyImage(binary);
    Raster_DestroyImage(gray);
    Raster_DestroyImage(source);
    return 0;
}
