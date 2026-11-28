#pragma once

#include "UiShell/Assembly/IMenuPresenter.h"

namespace AA
{
    // 默认菜单呈现器
    class MenuPresenter : public IMenuPresenter
    {
    public:
        MenuPresenter();
        ~MenuPresenter() override;

    public:
        QMenuBar* buildMenuBar(const QList<MenuNodeInfo>& menus,
            const ICommandCatalog* cmdCatalog,
            const ITextProvider* text,
            const ShellServices& services,
            QWidget* parent) const override;
    };
}  // namespace AA
