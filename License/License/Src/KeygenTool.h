#pragma once

#include <string>

namespace KeygenTool
{
bool GenerateKeyPair(const std::string& privKeyFile, const std::string& pubKeyFile);

std::string GenerateRegCode(
    const std::string& machineCode,
    const std::string& expiryDate,
    const std::string& features,
    const std::string& issueDate,
    const std::string& customerName,
    const std::string& privKeyFile);
} // namespace KeygenTool
