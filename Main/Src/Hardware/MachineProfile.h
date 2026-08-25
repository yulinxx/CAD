#pragma once

/**
 * @file MachineProfile.h
 * @brief 机器档案：一台**具体机器**的硬件配置。
 *
 * ==================== 为什么不放进客户 UI 配置 ====================
 *
 * 客户 UI 配置（`:/configs/<clientId>.json`）是编进可执行文件的 Qt 资源，
 * 描述的是「这个客户的产品长什么样」——菜单、工具栏、可见功能。
 *
 * 而机器档案描述的是「眼前这台机器接了什么卡、卡在几号槽、IP 是多少、
 * 急停接在哪一路输入」。这些值**每台机器都不同**，装机现场才知道。
 * 若塞进 qrc，等于每装一台机器就要重新编译一次可执行文件。
 *
 * 所以机器档案是**磁盘上的可写文件**，查找顺序：
 *   1. 环境变量 `SANYI_MACHINE_PROFILE` 指定的路径（现场调试/多机切换用，
 *      与 UiClientContext 的 `SANYI_CLIENT_ID` 同一套思路）；
 *   2. `<configDir>/machine.json`；
 *   3. 都没有 → 内置的模拟设备档案（见 builtinSimulatedProfile）。
 *
 * 第 3 条是刻意的：没有档案的机器（开发机、演示机、刚装好还没配的机器）
 * 必须能正常启动并进入模拟模式，而不是弹一个「找不到硬件配置」然后退出。
 * 但同时要在日志里 WARN 说清「当前是模拟设备，不是真机」——
 * 静默地把模拟当真机用，是这类兜底最危险的失败方式。
 */

#include <QString>
#include <QVector>
#include <QMap>

/**
 * @brief 一个逻辑 IO 点位的配置（对应 Hw::IoPointDef）。
 *
 * 这里用 Qt 类型而不是直接用 Hw::IoPointDef：本结构要参与 JSON 解析、
 * 要能在 BUILD_HARDWARE=OFF 时照常编译，也不该把 HAL 的 POD 约束
 * （定长 char[]）扩散到应用层的配置代码里。
 */
struct MachineIoPointConfig
{
    QString name;                ///< 逻辑名，如 "safety.door"
    QString label;               ///< 界面显示名，如 "安全门"
    int channel = -1;            ///< 物理通道号
    bool output = false;         ///< false=输入，true=输出
    bool analog = false;         ///< false=数字量，true=模拟量
    bool activeLow = false;      ///< 常闭接法的安全回路通常为 true
    int debounceMs = 0;          ///< 急停等安全点位应为 0
    double analogThreshold = 0.0;
    bool analogActiveBelow = true;
};

/**
 * @brief 一条安全条件的配置（对应 Hw::SafetyCondition）。
 */
struct MachineSafetyConditionConfig
{
    QString pointName;           ///< 关联点位名
    QString description;         ///< 违反时给用户看的文案
    bool triggerOnActive = true; ///< true=点位 active 时构成违反
    QString severity = "blocked";///< normal / warning / blocked / emergency
    QStringList actions;         ///< block_start / pause / laser_off / stop_motion / emergency
    bool violateWhenInvalid = true;  ///< fail-safe 默认：读不到就当违规
};

/**
 * @brief 一台机器的完整硬件档案。
 */
struct MachineProfile
{
    QString deviceId;                 ///< DeviceRegistry 里的设备 ID，如 "leadshine_ltdmc"
    QMap<QString, QString> openParams;///< IDevice::open() 的参数键值（值统一用字符串，与 Hw::ParamValue 一致）
    QVector<MachineIoPointConfig> ioPoints;
    QVector<MachineSafetyConditionConfig> safetyConditions;

    /// IO 轮询与安全评估的周期。HAL 建议 10–20ms。
    int tickIntervalMs = 20;

    /// 启动时是否自动 open 设备。现场排查接线时可以设 false，先只启动软件。
    bool autoOpen = true;

    // --- 以下由加载器填写，不出现在 JSON 里 ---

    /// true 表示这份档案来自内置兜底而非磁盘文件（当前跑的是模拟设备）。
    bool fromFallback = false;
    /// 实际读取的文件路径，用于日志与「关于」对话框。兜底时为空。
    QString sourcePath;
};

/**
 * @brief 机器档案加载器。
 */
namespace MachineProfileLoader
{
    /**
     * @brief 解析档案文件路径。
     * @param configDir 应用配置目录（AppPaths::configDir）
     * @return 存在的文件路径；都不存在时返回空字符串
     */
    QString resolvePath(const QString& configDir);

    /**
     * @brief 从 JSON 文件读取档案。
     * @param errorOut 失败原因（给日志与界面用）
     * @return 解析成功返回 true；失败时 profileOut 不被修改
     *
     * 解析失败**不会**回退到模拟设备：配置写错了却静默跑模拟，
     * 现场会以为「机器坏了」而不是「配置错了」。由调用方决定如何处理。
     */
    bool loadFromFile(const QString& path, MachineProfile& profileOut, QString& errorOut);

    /**
     * @brief 内置模拟设备档案。
     *
     * 带一组典型的安全点位（急停 + 安全门），使得没有真机时
     * 上层的安全链路也是「真的在跑」，而不是被绕过。
     */
    MachineProfile builtinSimulatedProfile();

    /**
     * @brief 按 resolvePath → loadFromFile → builtinSimulatedProfile 的顺序取档案。
     * @param warningOut 若发生了回退或解析失败，这里给出可直接展示的说明
     */
    MachineProfile loadOrFallback(const QString& configDir, QString& warningOut);
}
