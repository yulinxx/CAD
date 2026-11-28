#pragma once

#include <QString>

#include "UiShell/Contract/CustomerModule.h"

// 客户模块
class CustomerModuleProvider
{
public:
    bool load();

    // 最后一次加载失败信息
    const QString& lastError() const
    {
        return m_error;
    }

    const AA::CustomerModule& module() const
    {
        return m_module;
    }

    // 窗口标题
    QString title() const;

    // 语言切换后经 module.displayNameFn 按新语种重算显示名
    static QString titleFor(const AA::CustomerModule& module);

private:
    AA::CustomerModule m_module;

    // 客户持有者
    AA::ApiPtr<AA::IApiObject> m_owner;
    
    QString m_error;
};
