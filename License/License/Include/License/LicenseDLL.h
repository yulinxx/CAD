#ifndef LICENSE_LICENSE_DLL_H
#define LICENSE_LICENSE_DLL_H

/**
 * @file LicenseDLL.h
 * @brief License 模块的标准 C ABI 接口（可跨编译器 / 跨 IDE 使用）
 *
 * 设计要点：
 *   1) 纯 C 接口 —— 参数与返回值均为 POD 类型
 *   2) 禁止 STL 跨边界 —— 字符串通过 char* + length 传递
 *   3) 句柄模式 —— LicenseContext* 不透明指针管理生命周期
 *   4) 版本可查询 —— 主版本号变化表示 ABI 不兼容
 *
 * 典型调用顺序：
 *   License_ConfigInit  ->  License_Create  ->  License_Check / License_Activate
 *   ->  License_Destroy
 */

#include "LicenseAPI.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LICENSE_VERSION_MAJOR 1
#define LICENSE_VERSION_MINOR 0
#define LICENSE_VERSION_PATCH 0

#define LICENSE_MAKE_VERSION(major, minor, patch) \
    (((uint32_t)(major) << 16) | ((uint32_t)(minor) << 8) | (uint32_t)(patch))

#define LICENSE_VERSION \
    LICENSE_MAKE_VERSION( \
        LICENSE_VERSION_MAJOR, \
        LICENSE_VERSION_MINOR, \
        LICENSE_VERSION_PATCH)

    typedef enum LicenseResult
    {
        LICENSE_OK = 0,
        LICENSE_ERR_INVALID_ARG = -1,
        LICENSE_ERR_NULL_POINTER = -2,
        LICENSE_ERR_NOT_INITIALIZED = -3,
        LICENSE_ERR_BUFFER_TOO_SMALL = -4,
        LICENSE_ERR_IO = -5,
        LICENSE_ERR_VERIFY_FAILED = -6,
        LICENSE_ERR_EXPIRED = -7,
        LICENSE_ERR_VERSION_MISMATCH = -8,
        LICENSE_ERR_OUT_OF_RANGE = -9,
        LICENSE_ERR_INTERNAL = -99
    } LicenseResult;

    typedef enum LicenseGuardFlavor : uint32_t
    {
        LICENSE_GUARD_STARTUP = 0x1A3C,
        LICENSE_GUARD_SAVE = 0x2B4D,
        LICENSE_GUARD_EXPORT = 0x3C5E,
        LICENSE_GUARD_RENDER = 0x4D6F,
        LICENSE_GUARD_GENERIC = 0x5E7A
    } LicenseGuardFlavor;

    typedef struct LicenseContext LicenseContext;

    typedef struct LicenseConfig
    {
        uint32_t structSize;
        const char* configDir;
        int32_t enableCheck;
        uint32_t reserved0;
        uint32_t reserved1;
    } LicenseConfig;

    typedef struct LicenseInfo
    {
        uint32_t structSize;
        int32_t isValid;
        char machineCode[128];
        char expiryDate[16];
        char features[256];
        char issueDate[16];
        char customerName[128];
        char errorMessage[512];
        uint32_t reserved0;
        uint32_t reserved1;
    } LicenseInfo;

    LICENSE_C_API LICENSE_API uint32_t License_GetVersion(void);

    LICENSE_C_API LICENSE_API const char* License_GetVersionString(void);

    LICENSE_C_API LICENSE_API void License_ConfigInit(LicenseConfig* config);

    LICENSE_C_API LICENSE_API int License_IsCheckEnabled(void);

    LICENSE_C_API LICENSE_API void License_SetCheckEnabled(int enabled);

    LICENSE_C_API LICENSE_API LicenseContext* License_Create(const LicenseConfig* config);

    LICENSE_C_API LICENSE_API void License_Destroy(LicenseContext* ctx);

    LICENSE_C_API LICENSE_API int License_Check(LicenseContext* ctx);

    LICENSE_C_API LICENSE_API int License_Activate(LicenseContext* ctx, const char* regCode);

    LICENSE_C_API LICENSE_API int License_ReValidate(LicenseContext* ctx);

    LICENSE_C_API LICENSE_API int License_Clear(LicenseContext* ctx);

    LICENSE_C_API LICENSE_API int License_GetMachineCode(
        LicenseContext* ctx,
        char* buffer,
        size_t bufferSize);

    LICENSE_C_API LICENSE_API int License_GetInfo(
        LicenseContext* ctx,
        LicenseInfo* outInfo);

    LICENSE_C_API LICENSE_API int License_GuardMarkValid(void);

    LICENSE_C_API LICENSE_API int License_GuardMarkInvalid(void);

    LICENSE_C_API LICENSE_API int License_GuardRefresh(void);

    LICENSE_C_API LICENSE_API int License_GuardCheck(LicenseGuardFlavor flavor);

    LICENSE_C_API LICENSE_API int License_GuardIsQuickValid(void);

    LICENSE_C_API LICENSE_API int License_GetLastErrorMessage(
        char* buffer,
        size_t bufferSize);

#ifdef __cplusplus
}
#endif

#endif
