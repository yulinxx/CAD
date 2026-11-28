#pragma once

#include "UiShell/Contract/DialogRegistry.h"

namespace TS
{
    // TyvokStudio 的关于框工厂：以 AA::about::kAboutDlgId 注册，代替共享默认实现。
    // 只做三步：params → AboutDlgData → 模态打开 AboutDialog → 返回关闭方式。
    // 无状态；实例由客户模块的部件持有者（CustomerModule::owner）持有。
    class AboutDialogFactory final : public AA::IDialogFactory
    {
    public:
        AA::DialogResult openDialog(QWidget* parent, const QJsonObject& params = {}) override;
    };
}  // namespace TS
