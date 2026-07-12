#pragma once

#include <cstdint>
#include <string>

namespace LicenseInternal
{

bool IsCheckEnabled();
void SetCheckEnabled(bool enabled);

void CopyStringField(char* dest, size_t destSize, const std::string& value);

#ifdef LICENSE_TEST_HOOKS
std::string& GetTestPublicKeyOverride();
void SetTestPublicKeyOverride(const std::string& pem);
void ClearTestPublicKeyOverride();
#endif

} // namespace LicenseInternal
