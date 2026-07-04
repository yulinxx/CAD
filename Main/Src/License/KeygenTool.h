#pragma once

#include <string>

// 开发工具：生成密钥对和注册码
// 编译为独立工具，不在主程序中使用
namespace KeygenTool
{
    bool GenerateKeyPair(const std::string& privKeyFile, const std::string& pubKeyFile);

    std::string GenerateRegCode(const std::string& machineCode,
                                const std::string& expiryDate,
                                const std::string& features,
                                const std::string& issueDate,
                                const std::string& customerName,
                                const std::string& privKeyFile);
}
