#include "License/LicenseTestHooks.h"
#include "LicenseInternal.h"
#include "LicenseThrottle.h"

#ifdef LICENSE_TEST_HOOKS

extern "C"
{
    int LicenseTest_SetPublicKeyPem(const char* pem)
    {
        if (!pem || pem[0] == '\0')
        {
            return LICENSE_ERR_INVALID_ARG;
        }

        LicenseInternal::SetTestPublicKeyOverride(pem);
        return LICENSE_OK;
    }

    void LicenseTest_ClearPublicKeyPem(void)
    {
        LicenseInternal::ClearTestPublicKeyOverride();
    }

    int LicenseTest_SetThrottleConfig(
        int freeAttempts, int baseDelaySec, int maxDelaySec, int hardLockFailures, int hardLockSec)
    {
        // 参数合法性校验，防止测试传入自相矛盾的节流策略
        if (freeAttempts < 0 || baseDelaySec < 0 || maxDelaySec < baseDelaySec || hardLockFailures <= freeAttempts ||
            hardLockSec <= 0)
        {
            return LICENSE_ERR_INVALID_ARG;
        }

        LicenseThrottle::Config config;
        config.freeAttempts = freeAttempts;
        config.baseDelaySec = baseDelaySec;
        config.maxDelaySec = maxDelaySec;
        config.hardLockFailures = hardLockFailures;
        config.hardLockSec = hardLockSec;
        LicenseThrottle::SetConfig(config);
        return LICENSE_OK;
    }

    void LicenseTest_ResetThrottleConfig(void)
    {
        LicenseThrottle::SetConfig(LicenseThrottle::Config{});
    }
}

#endif