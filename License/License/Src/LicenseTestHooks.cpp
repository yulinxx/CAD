#include "License/LicenseTestHooks.h"
#include "LicenseInternal.h"

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
}

#endif