#pragma once

#include <QJsonObject>
#include <QString>

#include "UiDialogs/SampleSolver.h"
#include "UiShell/Contract/DialogRegistry.h"

class QWidget;

namespace RD::samples
{
    // 示例：「参数对话框 → 命令 → 算法」的完整链路，新增算法对话框照此编写
    //   客户代码：CustomerModule::listDialogBind 把命令 sample.run 绑到 dialog.sample.params，params 写本客户的默认值 / 可选项；
    //   对话框：读 params 作初值，确定后把 SampleParams::toJson() 放进 DialogResult.data；
    //   命令：客户命令类在 invokeWithArgs 里调用 runSampleCommand()，按 args 运行 solveSquareRoot()。
    inline constexpr const char* kSampleParamsDlgId = "dialog.sample.params";
    inline constexpr const char* kSampleRunCmdId = "sample.run";

    // dialog.sample.params 的共享默认实现。除 SampleParams 的键外，params 还支持
    // methods（字符串数组）限制可选方法，用于在客户代码绑定里表达客户差异。
    // 工厂无状态：需要它的客户在自己的部件持有者（CustomerModule::owner）里放一个实例并注册，不提供全局实例。
    class SampleParamsDialogFactory final : public IDialogFactory
    {
    public:
        DialogResult openDialog(QWidget* parent, const QJsonObject& params = {}) override;
    };

    // 命令处理：commandId 为 sample.run 时按 args 运行示例算法、弹框显示结果并返回 true；其它命令返回 false。
    // parent 为空时以当前活动窗口为父。
    bool runSampleCommand(const QString& commandId, const QJsonObject& args, QWidget* parent = nullptr);

    // 已翻译的方法名与结果文字：默认实现与客户自定义界面共用。
    QString methodDisplayName(const QString& methodId);
    QString describeResult(const SampleParams& params, const SampleResult& result);
}  // namespace RD::samples
