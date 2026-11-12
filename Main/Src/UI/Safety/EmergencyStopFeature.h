#pragma once

/**
 * @file EmergencyStopFeature.h
 * @brief 急停功能总开关（单点定义，全仓只引用这里）
 *
 * 软件尚未交付急停硬件链路，v1.1 开启。关闭时的行为：
 * - 不创建、不显示常驻急停浮层；
 * - 不读取、不写入 safety/emergencyStopLatched（ini 里残留 true 也不会进入只读）；
 * - MenuDispatcher 不设急停只读闸门，激光启动校验不做急停判断。
 *
 * 开启时需要同时恢复以下五处（都引用本开关，不要另设判断）：
 * 1. WorkbenchWindow::bindSafetyServices —— 创建浮层并接线只读态；
 * 2. EmergencyStopOverlay::syncVisibility —— 按状态驱动显示/隐藏；
 * 3. MenuDispatcher::dispatch —— 只读白名单拦截（日志保留，开启后需复测）；
 * 4. LaserOperationRegistry —— 「开始加工」的 !isReadOnly() 校验；
 * 5. EmergencyStopController —— 闭锁的加载与持久化。
 */
namespace EmergencyStopFeature
{
inline constexpr bool kEnabled = false;
}
