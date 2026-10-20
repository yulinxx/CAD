#pragma once

#include <cstdint>
#include <string>

#ifdef _MSC_VER
    #pragma section("secure", read)
    #define SECURE_SECTION __declspec(allocate("secure"))
#else
    #define SECURE_SECTION
#endif

namespace StrEncrypt
{

    inline std::string Decrypt(const uint8_t* data, size_t len)
    {
        if (len < 2)
        {
            return {};
        }

        uint8_t key = data[0];
        std::string result;
        result.reserve(len - 1);
        for (size_t i = 1; i < len; ++i)
        {
            result.push_back(static_cast<char>(data[i] ^ static_cast<uint8_t>(key + i - 1)));
        }
        return result;
    }

}  // namespace StrEncrypt

#define DEFINE_ENCRYPTED_STR(name, ...)                                                         \
    SECURE_SECTION static const uint8_t name##_encrypted[] = { __VA_ARGS__ };                   \
    inline const std::string& name()                                                            \
    {                                                                                           \
        static std::string s = StrEncrypt::Decrypt(name##_encrypted, sizeof(name##_encrypted)); \
        return s;                                                                               \
    }
