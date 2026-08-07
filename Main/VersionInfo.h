#pragma once

#include <string>

#define APP_NAME "SanYiCAD"
#define APP_VERSION "1.0.0"
#define APP_ORGANIZATION_NAME "SanYi"
#define APP_ORGANIZATION_DOMAIN "sanyi-cad.com"

#define VERSION_MAJOR 1
#define VERSION_MINOR 0
#define VERSION_PATCH 0

namespace MainApp
{
    inline const std::string appName() { return APP_NAME; }
    inline const std::string appVersion() { return APP_VERSION; }
    inline const std::string organizationName() { return APP_ORGANIZATION_NAME; }
    inline const std::string organizationDomain() { return APP_ORGANIZATION_DOMAIN; }
    
    inline int versionMajor() { return VERSION_MAJOR; }
    inline int versionMinor() { return VERSION_MINOR; }
    inline int versionPatch() { return VERSION_PATCH; }
}
