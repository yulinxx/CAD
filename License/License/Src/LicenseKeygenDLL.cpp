#include "License/LicenseKeygen.h"
#include "KeygenTool.h"

#include <cstring>
#include <string>

extern "C"
{
    int LicenseKeygen_GenerateKeyPair(const char* privateKeyPath, const char* publicKeyPath)
    {
        try
        {
            if (!privateKeyPath || privateKeyPath[0] == '\0' || !publicKeyPath || publicKeyPath[0] == '\0')
            {
                return LICENSE_ERR_INVALID_ARG;
            }

            return KeygenTool::GenerateKeyPair(privateKeyPath, publicKeyPath) ? LICENSE_OK : LICENSE_ERR_IO;
        }
        catch (...)
        {
            return LICENSE_ERR_INTERNAL;
        }
    }

    int LicenseKeygen_GenerateRegCode(
        const char* machineCode,
        const char* expiryDate,
        const char* features,
        const char* issueDate,
        const char* customerName,
        const char* privateKeyPath,
        char* buffer,
        size_t bufferSize)
    {
        try
        {
            if (!machineCode || !expiryDate || !features || !issueDate || !customerName || !privateKeyPath)
            {
                return LICENSE_ERR_INVALID_ARG;
            }

            if (!buffer || bufferSize == 0)
            {
                return LICENSE_ERR_NULL_POINTER;
            }

            const std::string regCode = KeygenTool::GenerateRegCode(
                machineCode,
                expiryDate,
                features,
                issueDate,
                customerName,
                privateKeyPath);

            if (regCode.empty())
            {
                return LICENSE_ERR_IO;
            }

            if (regCode.size() + 1 > bufferSize)
            {
                return LICENSE_ERR_BUFFER_TOO_SMALL;
            }

            std::memcpy(buffer, regCode.c_str(), regCode.size() + 1);
            return LICENSE_OK;
        }
        catch (...)
        {
            return LICENSE_ERR_INTERNAL;
        }
    }
}