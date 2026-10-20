#pragma once

/**
 * @file LicenseTestHooks.h
 * @brief 仅供单元测试使用的公钥注入接口（BUILD_LICENSE_TESTS 时编译进 DLL）
 *
 * 允许测试用临时 RSA 密钥对完成激活闭环，不影响 Release 默认构建。
 */

#include "LicenseAPI.h"
#include "License/LicenseDLL.h"

#ifdef LICENSE_TEST_HOOKS

    #ifdef __cplusplus
extern "C"
{
    #endif

    LICENSE_C_API LICENSE_API int LicenseTest_SetPublicKeyPem(const char* pem);

    LICENSE_C_API LICENSE_API void LicenseTest_ClearPublicKeyPem(void);

    // 覆盖激活失败节流参数（测试用短延迟/低阈值），参数非法时返回 LICENSE_ERR_INVALID_ARG
    LICENSE_C_API LICENSE_API int LicenseTest_SetThrottleConfig(
        int freeAttempts, int baseDelaySec, int maxDelaySec, int hardLockFailures, int hardLockSec);

    // 恢复节流参数为生产默认值
    LICENSE_C_API LICENSE_API void LicenseTest_ResetThrottleConfig(void);

    #ifdef __cplusplus
}
    #endif

#endif  // LICENSE_TEST_HOOKS
