/**
 * @file TransformInputProvider.h
 * @brief 变换参数输入接口 — 让 Operation 不直接依赖 UI
 *
 * 这是一个抽象接口，用于收集变换操作所需的参数。
 * 不同的实现可以支持不同的输入方式：
 * - TransformDialogAdapter: 对话框输入
 * - MouseInteractionAdapter: 鼠标交互输入
 * - ScriptAdapter: 脚本输入
 *
 * 设计目标：
 * - 让 Operation 不直接依赖 UI
 * - 让参数收集和参数使用分离
 * - 支持多种输入方式
 */
#pragma once

#include "UI/TransformParameters.h"
#include <functional>

 /**
  * @brief 变换参数输入接口 — 让 Operation 不直接依赖 UI
  *
  * 这是一个抽象接口，用于收集变换操作所需的参数。
  * 不同的实现可以支持不同的输入方式：
  * - TransformDialogAdapter: 对话框输入
  * - MouseInteractionAdapter: 鼠标交互输入
  * - ScriptAdapter: 脚本输入
  */
class ITransformInputProvider
{
public:
    virtual ~ITransformInputProvider() = default;

    /**
     * @brief 获取变换参数
     * @return 变换参数
     *
     * 这是一个阻塞调用，会等待用户输入完成
     */
    virtual TransformParameters getParameters() = 0;

    /**
     * @brief 检查是否有有效的参数
     * @return true 表示有有效参数
     */
    virtual bool hasValidParameters() const = 0;

    /**
     * @brief 获取当前参数（不阻塞）
     * @return 当前参数
     */
    virtual TransformParameters currentParameters() const = 0;

    /**
     * @brief 设置参数变更回调
     * @param callback 参数变更时调用的回调函数
     */
    virtual void setParametersChangedCallback(std::function<void(const TransformParameters&)> callback) = 0;

    /**
     * @brief 设置确认回调
     * @param callback 用户确认时调用的回调函数
     */
    virtual void setConfirmedCallback(std::function<void(bool confirmed)> callback) = 0;

    /**
     * @brief 取消操作
     */
    virtual void cancel() = 0;

    /**
     * @brief 获取变换类型
     * @return 变换类型
     */
    virtual TransformType transformType() const = 0;
};
