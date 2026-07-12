#ifndef LICENSE_LICENSE_KEYGEN_H
#define LICENSE_LICENSE_KEYGEN_H

/**
 * @file LicenseKeygen.h
 * @brief License 密钥生成 C ABI（仅供内部 KeygenTool 使用，不随主程序分发）
 *
 * 此头文件提供 RSA 密钥对生成与注册码签名能力。
 * 私钥操作不应出现在客户端应用中。
 */

#include "LicenseAPI.h"
#include "License/LicenseDLL.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

    LICENSE_C_API LICENSE_API int LicenseKeygen_GenerateKeyPair(
        const char* privateKeyPath,
        const char* publicKeyPath);

    LICENSE_C_API LICENSE_API int LicenseKeygen_GenerateRegCode(
        const char* machineCode,
        const char* expiryDate,
        const char* features,
        const char* issueDate,
        const char* customerName,
        const char* privateKeyPath,
        char* buffer,
        size_t bufferSize);

#ifdef __cplusplus
}
#endif

#endif
