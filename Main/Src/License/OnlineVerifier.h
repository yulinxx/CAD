#pragma once

#include <memory>
#include <string>

struct LicenseInfo;

// ---------------------------------------------------------------
// 在线验证接口（扩展预留）
// 当前为离线验证，后续如需联网验证，实现此接口并注入 LicenseManager
// ---------------------------------------------------------------
class IOnlineVerifier
{
public:
    virtual ~IOnlineVerifier() = default;

public:
    // 联网验证许可有效性
    // 返回 true = 在线验证通过（或降级通过）
    virtual bool Verify(const LicenseInfo& info) = 0;

    // 在线服务是否可用（网络可达、服务器正常等）
    virtual bool IsAvailable() const = 0;

    // 验证器名称（日志/显示用）
    virtual const char* GetName() const = 0;

    // 是否要求强制在线验证
    // true  = 离线验证通过但在线失败时，拒绝启动
    // false = 在线验证仅用于增强，离线通过即可
    virtual bool IsRequired() const { return false; }
};
