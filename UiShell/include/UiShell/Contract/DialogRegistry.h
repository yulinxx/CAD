#pragma once

#include <QJsonObject>
#include <QString>

#include "UiShell/Export.h"
#include "UiShell/Contract/IApiObject.h"

class QWidget;

namespace AA
{
    // 对话框返回结果
    struct DialogResult
    {
        enum Status
        {
            Accepted,  // 确认
            Rejected,  // 取消或关闭
            Cancelled  // 未打开
        };

        Status status = Cancelled;
        QJsonObject data;  // 返回的结构化数据
    };

    namespace DialogParam
    {
        constexpr const char* Theme = "theme";            // 皮肤 id
        constexpr const char* Language = "language";      // 语言
        constexpr const char* AppName = "appName";        // 
        constexpr const char* AppVersion = "appVersion";  // 
        constexpr const char* Series = "series";          // 皮肤品牌系列
        // M5：profile 的 brand 段注入的通用品牌参数，显式 dialog binding 优先。
        constexpr const char* Product = "product";
        constexpr const char* Company = "company";
        constexpr const char* Copyright = "copyright";
        constexpr const char* Website = "website";
        constexpr const char* Email = "email";
        constexpr const char* License = "license";
        constexpr const char* LogoPath = "logoPath";
    }  // namespace DialogParam

    // 对话框工厂接口
    class IDialogFactory
    {
    public:
        virtual ~IDialogFactory() = default;

        // 创建并显示对话框
        virtual DialogResult openDialog(QWidget* parent, const QJsonObject& params = {}) = 0;
    };

    // 对话框工厂注册
    class IDialogFactoryRegistry : public IApiObject
    {
    protected:
        // 只能经 destroy()释放，禁止跨模块直接 delete
        ~IDialogFactoryRegistry() override = default;

    public:
        // 注册
        virtual void registerFactory(const QString& dialogId, IDialogFactory* factory) = 0;

        // 查询对对话框的工厂
        virtual IDialogFactory* find(const QString& dialogId) const = 0;
    };

    // 创建对话框工厂注册表，由 shell 创建，调用方通过 destroy() 释放
    UI_SHELL_API IDialogFactoryRegistry* createDialogRegistry();
}  // namespace AA
