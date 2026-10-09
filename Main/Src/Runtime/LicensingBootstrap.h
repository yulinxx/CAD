#pragma once
/**
 * @file LicensingBootstrap.h
 * @brief 启动期授权闸门（试用期检查 + 许可激活 + FeatureGate 注入）
 *
 * 从 CADApplicationRuntime::run 抽出（审查文档 §3.5.5 / P2-2.7）：
 * run() 只负责编排（初始化 → 授权闸门 → 引导 → 事件循环），
 * 授权/许可的 UI 流程全部收敛在本单元。
 */
#include <QString>

namespace LicensingBootstrap
{
    /**
     * @brief 执行授权闸门：试用期对话框 → 许可检查 → UiFeatureGate 注入
     *
     * @param configDir 许可配置目录（License/Trial 的持久化位置）
     * @return 0 = 继续启动；负值 = 应直接作为进程退出码返回
     *         （-1 用户选择退出试用/取消激活，-3 许可被用户拒绝）
     */
    int runGate(const QString& configDir);
}  // namespace LicensingBootstrap
