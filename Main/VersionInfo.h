#pragma once

#include <string>

#define APP_NAME "SanYiCAD"
#define APP_VERSION "1.0.0"
#define APP_ORGANIZATION_NAME "SanYi"
#define APP_ORGANIZATION_DOMAIN "sanyi-cad.com"

#define VERSION_MAJOR 1
#define VERSION_MINOR 0
#define VERSION_PATCH 0

// 构建时间（由 CMake 注入）
#define APP_BUILD_TIME "2026-09-24 06:19:09"
#define APP_BUILD_DATE "2026-09-24"
#define APP_BUILD_TYPE "Release"

namespace MainApp
{
    inline const std::string appName() { return APP_NAME; }
    inline const std::string appVersion() { return APP_VERSION; }
    inline const std::string organizationName() { return APP_ORGANIZATION_NAME; }
    inline const std::string organizationDomain() { return APP_ORGANIZATION_DOMAIN; }

    inline int versionMajor() { return VERSION_MAJOR; }
    inline int versionMinor() { return VERSION_MINOR; }
    inline int versionPatch() { return VERSION_PATCH; }

    inline const std::string buildTime() { return APP_BUILD_TIME; }
    inline const std::string buildDate() { return APP_BUILD_DATE; }
    inline const std::string buildType() { return APP_BUILD_TYPE; }
}
