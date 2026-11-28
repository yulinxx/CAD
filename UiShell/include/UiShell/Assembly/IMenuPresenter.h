#pragma once

#include "UiShell/Export.h"
#include "UiShell/Model/AssemblyInfo.h"
#include "UiShell/Assembly/ShellServices.h"

#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QStringList>
#include <QIcon>
#include <vector>

namespace AA
{
    class ICommandCatalog;
    class ITextProvider;

    // 菜单呈现策略接口
    class IMenuPresenter
    {
    public:
        virtual ~IMenuPresenter() = default;

    public:
        // 构建菜单
        virtual QMenuBar* buildMenuBar(const QList<MenuNodeInfo>& menus,
            const ICommandCatalog* cmdCatalog,
            const ITextProvider* text,
            const ShellServices& services,
            QWidget* parent) const = 0;
    };

    // 创建默认菜单
    UI_SHELL_API std::unique_ptr<IMenuPresenter> createMenuPresenter();
}  // namespace AA
