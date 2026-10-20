#include "License/LicenseDLL.h"

#include <cstdio>
#include <string>

// ============================================================
//  License C ABI 使用示例
//  编译后执行 SyLicenseExample.exe
//
//  典型调用顺序（见 LicenseDLL.h）：
//    License_ConfigInit -> License_Create -> License_Check / License_GetInfo
//    -> License_Destroy
//
//  本示例使用 enableCheck=0（演示模式）与本地配置目录，
//  不写入用户 AppData，可安全反复运行。
// ============================================================

int main()
{
    std::printf("=== SyLicenseExample ===\n");
    std::printf("Version: %s (0x%08X)\n",
        License_GetVersionString(), License_GetVersion());

    // ---- 1. 配置初始化 ----------------------------------------------------
    LicenseConfig config;
    License_ConfigInit(&config);
    const std::string configDir = "SyLicenseExample_Config";
    config.configDir = configDir.c_str();
    config.enableCheck = 0;  // 演示模式：不启用校验

    // ---- 2. 创建上下文 ----------------------------------------------------
    LicenseContext* ctx = License_Create(&config);
    if (!ctx)
    {
        std::printf("License_Create failed\n");
        return 1;
    }
    std::printf("Check enabled: %d\n", License_IsCheckEnabled());

    // ---- 3. 机器码 / 校验 / 信息 ------------------------------------------
    char machine[128] = {};
    if (License_GetMachineCode(ctx, machine, sizeof(machine)) == LICENSE_OK)
        std::printf("Machine code: %s\n", machine);
    else
        std::printf("GetMachineCode failed\n");

    const int check = License_Check(ctx);
    std::printf("License_Check -> %d (%s)\n", check,
        check == LICENSE_OK ? "OK" : "not activated");

    LicenseInfo info{};
    info.structSize = sizeof(info);
    if (License_GetInfo(ctx, &info) == LICENSE_OK)
    {
        std::printf("Info: valid=%d customer='%s' expiry='%s'\n",
            info.isValid, info.customerName, info.expiryDate);
    }

    // ---- 4. 销毁 ----------------------------------------------------------
    License_Destroy(ctx);
    std::printf("Done.\n");
    return 0;
}
