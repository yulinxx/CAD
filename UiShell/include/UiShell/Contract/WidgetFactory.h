#pragma once

#include <QJsonObject>
#include <QString>

#include "UiShell/Export.h"
#include "UiShell/Contract/HostContext.h"
#include "UiShell/Contract/IApiObject.h"
#include "UiShell/Contract/TitleBarSpec.h"

class QWidget;

namespace AA
{
    class IShellHost;
    class ITitleBarHost;

    struct PanelCreateRequest
    {
        QString strPanelId;
        QString strFactoryId;
        QJsonObject props;
        HostContext context;
        IShellHost* host = nullptr;
        QWidget* parent = nullptr;
    };

    // 参数校验
    struct PropsValidation
    {
        bool bValid = true;
        QString strMsg;
    };

    // Widget 控件创建工厂
    class IWidgetFactory
    {
    public:
        virtual ~IWidgetFactory() = default;

    public:
        virtual QWidget* create(const PanelCreateRequest& info) = 0;

        virtual PropsValidation validateProps(const QJsonObject& props) const
        {
            Q_UNUSED(props);
            return {};
        }
    };

    class IWidgetFactoryRegistry : public IApiObject
    {
    protected:
        // 只能经 destroy() 释放，禁止跨模块直接 delete。
        ~IWidgetFactoryRegistry() override = default;

    public:
        virtual void registerFactory(const QString& factoryId, IWidgetFactory* factory) = 0;

        virtual IWidgetFactory* find(const QString& factoryId) const = 0;
    };

    // 创建工厂注册表，由 shell 创建，调用方通过 destroy() 释放。
    UI_SHELL_API IWidgetFactoryRegistry* createWidgetFactoryRegistry();

    class ICenterViewProvider
    {
    public:
        virtual ~ICenterViewProvider() = default;

    public:
        // 创建中央视图
        virtual QWidget* createCenterView(QWidget* parent) = 0;
    };

    class ITitleBarFactory
    {
    public:
        virtual ~ITitleBarFactory() = default;

    public:
        virtual QWidget* createTitleBar(const TitleBarSpec& spec, ITitleBarHost* host, QWidget* parent) = 0;
    };
}  // namespace AA
