#pragma once

#include <QString>
#include <QStringList>

class QWidget;

namespace RD
{
    // 区域容器查询接口；控件由 shell 创建并随控件树销毁，调用方不能 destroy/delete
    class IRegionContainer
    {
    protected:
        virtual ~IRegionContainer() = default;

    public:
        virtual QWidget* widget() = 0;
        virtual QString regionId() const = 0;
        virtual QStringList panelIds() const = 0;
        virtual bool isPanelMounted(const QString& panelId) const = 0;
        virtual QString paneAreaIdForPanel(const QString& panelId) const = 0;
        virtual QWidget* panelWidget(const QString& panelId) const = 0;
        virtual QWidget* paneAreaWidget(const QString& paneAreaId) const = 0;
    };
}  // namespace RD
