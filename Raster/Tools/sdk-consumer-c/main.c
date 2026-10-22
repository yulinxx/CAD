/*
 * Raster SDK smoke test: plain C consumer through manual include/lib wiring.
 *
 * Guards the contract a C-only integrator depends on:
 *   - the public headers compile as C99;
 *   - the C ABI is usable without any C++ runtime;
 *   - handle lifecycle (create / use / destroy) is leak-free and crash-free.
 *
 * Prints "consumer-c ok ..." on success and returns 0.
 */

#include <Raster/RasterApi.h>

#include <stdio.h>
#include <string.h>

int main(void)
{
    RasterImage source = NULL;
    RasterImage gray = NULL;
    RasterImage binary = NULL;
    RasterImageInfo info;
    RasterStats stats;
    RasterStatus status;

    if (Raster_GetVersion() != RASTER_VERSION) {
        fprintf(stderr, "version mismatch: runtime=%u header=%u\n", Raster_GetVersion(),
                (unsigned int)RASTER_VERSION);
        return 1;
    }
    if (Raster_GetAbiVersion() != RASTER_ABI_VERSION) {
        fprintf(stderr, "abi mismatch\n");
        return 2;
    }

    /* 建图、填内容：公开头提供的是纯 POD 视图，调用方自己管内存。 */
    status = Raster_CreateImage(64, 48, RASTER_FORMAT_GRAY8, &source);
    if (status != RASTER_OK) {
        fprintf(stderr, "Raster_CreateImage failed: %s\n", Raster_StatusText(status));
        return 3;
    }

    {
        RasterImageView view;
        unsigned char* pixels;
        int x;
        int y;
        memset(&view, 0, sizeof(view));
        view.structSize = (int32_t)sizeof(view);
        if (Raster_GetImageView(source, &view) != RASTER_OK) {
            fprintf(stderr, "Raster_GetImageView failed\n");
            Raster_DestroyImage(source);
            return 4;
        }
        pixels = (unsigned char*)view.data;
        for (y = 0; y < view.height; ++y) {
            for (x = 0; x < view.width; ++x) {
                pixels[(size_t)y * (size_t)view.stride + (size_t)x] = (unsigned char)(x * 4);
            }
        }
    }

    status = Raster_ToGray(source, &gray);
    if (status != RASTER_OK) {
        fprintf(stderr, "Raster_ToGray failed: %s\n", Raster_StatusText(status));
        Raster_DestroyImage(source);
        return 5;
    }

    status = Raster_Threshold(gray, RASTER_THRESH_OTSU, 0.0, 255.0, &binary);
    if (status != RASTER_OK) {
        fprintf(stderr, "Raster_Threshold failed: %s\n", Raster_StatusText(status));
        Raster_DestroyImage(gray);
        Raster_DestroyImage(source);
        return 6;
    }

    memset(&info, 0, sizeof(info));
    info.structSize = (int32_t)sizeof(info);
    if (Raster_GetImageInfo(binary, &info) != RASTER_OK) {
        fprintf(stderr, "Raster_GetImageInfo failed\n");
        Raster_DestroyImage(binary);
        Raster_DestroyImage(gray);
        Raster_DestroyImage(source);
        return 7;
    }
    if (info.width != 64 || info.height != 48 || info.channels != 1) {
        fprintf(stderr, "unexpected image geometry: %dx%d ch=%d\n", (int)info.width,
                (int)info.height, (int)info.channels);
        Raster_DestroyImage(binary);
        Raster_DestroyImage(gray);
        Raster_DestroyImage(source);
        return 8;
    }

    memset(&stats, 0, sizeof(stats));
    stats.structSize = (int32_t)sizeof(stats);
    if (Raster_ComputeStats(binary, &stats) != RASTER_OK) {
        fprintf(stderr, "Raster_ComputeStats failed\n");
        Raster_DestroyImage(binary);
        Raster_DestroyImage(gray);
        Raster_DestroyImage(source);
        return 9;
    }

    /* 失败路径也必须给得出可读原因，而不是静默失败。 */
    {
        char message[256];
        RasterImage missing = NULL;
        (void)Raster_LoadImage("__definitely_not_a_real_file__.png", &missing);
        if (Raster_GetLastErrorMessage(message, sizeof(message)) <= 0) {
            fprintf(stderr, "expected a diagnostic message for the failed load\n");
            Raster_DestroyImage(binary);
            Raster_DestroyImage(gray);
            Raster_DestroyImage(source);
            return 10;
        }
    }

    Raster_DestroyImage(binary);
    Raster_DestroyImage(gray);
    Raster_DestroyImage(source);

    printf("consumer-c ok: version=%s abi=%u ink=%.3f\n", Raster_GetVersionString(),
           Raster_GetAbiVersion(), stats.inkRatio);
    return 0;
}
