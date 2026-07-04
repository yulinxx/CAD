#pragma once

#include <cstdint>
#include <string>

// ---------------------------------------------------------------
// 字符串加密工具（防静态搜索）
// 将敏感字符串（公钥、错误信息等）放在单独 PE 节区并运行期 XOR 解密，
// 使反汇编工具和静态字符串搜索无法直接定位。
// ---------------------------------------------------------------

#ifdef _MSC_VER
#pragma section("secure", read)
#define SECURE_SECTION __declspec(allocate("secure"))
#else
#define SECURE_SECTION
#endif

namespace StrEncrypt
{

// 解密 XOR 加密的字符串
// data[0] = key, data[1..n] = ch XOR (key + idx)
inline std::string Decrypt(const uint8_t* data, size_t len)
{
    if (len < 2)
        return {};
    uint8_t key = data[0];
    std::string result;
    result.reserve(len - 1);
    for (size_t i = 1; i < len; ++i)
        result.push_back(static_cast<char>(data[i] ^ static_cast<uint8_t>(key + i - 1)));
    return result;
}

// 返回缓存的解密结果（重复调用不重复计算）
template<const uint8_t* Data, size_t Len>
inline const std::string& Cached()
{
    static std::string s = Decrypt(Data, Len);
    return s;
}

} // namespace StrEncrypt

// 快捷宏：定义加密字节数组 + 获取解密字符串的辅助函数
#define DEFINE_ENCRYPTED_STR(name, ...)                                    \
    SECURE_SECTION static const uint8_t name##_encrypted[] = { __VA_ARGS__ }; \
    inline const std::string& name()                                       \
    {                                                                      \
        static std::string s = StrEncrypt::Decrypt(                        \
            name##_encrypted, sizeof(name##_encrypted));                   \
        return s;                                                          \
    }
