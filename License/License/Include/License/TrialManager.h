#pragma once

#include <string>

/**
 * @file TrialManager.h
 * @brief 试用期管理模块（跨平台）
 *
 * 设计原则：
 * - 低耦合：仅依赖 MachineFingerprint 和基础文件系统
 * - 跨平台：Windows / macOS / Linux
 * - 防绕过：文件 + 机器指纹双重绑定
 *
 * 存储位置：
 * - Windows: 注册表 HKEY_CURRENT_USER\Software\SanYiCAD\Trial + 文件
 * - macOS/Linux: 文件 ~/.config/sanyicad/trial.info
 */

#ifdef __cplusplus
extern "C"
{
#endif

    // C 接口（供 Runtime 调用）
    // 返回值：0=成功, -1=试用期已过期, -2=初始化失败
    LICENSE_C_API LICENSE_API int Trial_Check(int* remainingDays);

    // 清除试用记录（供测试/重置使用）
    LICENSE_C_API LICENSE_API void Trial_Reset();

    // 设置配置目录（需要在 check 之前调用）
    LICENSE_C_API LICENSE_API void Trial_SetConfigDir(const char* dir);

#ifdef __cplusplus
}
#endif

// C++ 接口
#ifdef __cplusplus

#include <memory>

class TrialManager
{
public:
    /// 获取单例
    static TrialManager& instance();

    /// 试用期状态
    enum class Status
    {
        NotStarted,  // 未开始（异常状态）
        Active,      // 试用中
        Expired      // 已过期
    };

    /// 检查试用期状态
    Status check();

    /// 剩余试用天数（-1 表示已过期）
    int remainingDays() const;

    /// 是否处于试用模式（功能无限制）
    bool isActive() const;

    /// 试用开始日期
    std::string startDate() const;

    /// 试用天数配置
    static constexpr int kDefaultTrialDays = 30;

    /// 禁用拷贝
    TrialManager(const TrialManager&) = delete;
    TrialManager& operator=(const TrialManager&) = delete;

private:
    TrialManager() = default;
    ~TrialManager() = default;

    /// 初始化试用期（首次启动时调用）
    void init();

    /// 验证试用记录有效性
    bool validate() const;

    /// 检查机器指纹是否匹配
    bool checkMachineMatch() const;

    /// 保存到文件
    bool saveToFile(const std::string& configDir) const;

    /// 从文件加载
    bool loadFromFile(const std::string& configDir);

    /// 保存到注册表（仅 Windows）
    bool saveToRegistry() const;

    /// 从注册表加载（仅 Windows）
    bool loadFromRegistry();

    /// 获取当前日期
    static std::string getCurrentDate();

    /// 计算日期之间的天数差
    static int daysBetween(const std::string& start, const std::string& end);

    // 成员变量
    std::string m_startDate;
    std::string m_machineHash;
    int m_trialDays = kDefaultTrialDays;
    bool m_initialized = false;
    Status m_status = Status::NotStarted;
};

#endif  // __cplusplus
