#include "UiContextMenuService.h"

#include "UiClientConfigBase.h"
#include "UiLayoutBuilder.h"
#include "UiPanelRegistry.h"

#include "Log/SyLogger.h"

#include <QAction>
#include <QMenu>

#include <algorithm>

namespace
{
    /// 在配置里按 id 查找右键菜单定义
    const ContextMenuDef* findContextMenu(const UiConfigData* config, const QString& id)
    {
        if (!config || id.isEmpty())
        {
            return nullptr;
        }
        auto it = std::find_if(config->contextMenus.cbegin(),
            config->contextMenus.cend(),
            [&id](const ContextMenuDef& def) {
                return def.id == id;
            });
        return it != config->contextMenus.cend() ? &(*it) : nullptr;
    }

    /// 菜单是否含有真实可用条目（分隔符不算）
    bool hasRealAction(const QMenu* menu)
    {
        if (!menu)
        {
            return false;
        }
        for (QAction* act : menu->actions())
        {
            if (act && !act->isSeparator())
            {
                return true;
            }
        }
        return false;
    }
}  // namespace

UiContextMenuService& UiContextMenuService::instance()
{
    static UiContextMenuService service;
    return service;
}

void UiContextMenuService::registerDynamicSection(const QString& sectionId, UiMenuSectionFiller filler)
{
    if (sectionId.isEmpty() || !filler)
    {
        SY_WARN("[UiContextMenuService] registerDynamicSection called with empty id or null filler, ignored");
        return;
    }
    const bool replaced = m_sections.contains(sectionId);
    m_sections.insert(sectionId, std::move(filler));
}
}

void UiContextMenuService::unregisterDynamicSection(const QString& sectionId)
{
    m_sections.remove(sectionId);
}

int UiContextMenuService::fillDynamicSections(QMenu* menu, const QStringList& sectionIds, const QString& ownerId)
{
    if (!menu)
    {
        return 0;
    }

    int added = 0;
    // 顺序严格按 JSON 中 dynamicSections 的声明顺序
    for (const QString& sectionId : sectionIds)
    {
        auto it = m_sections.constFind(sectionId);
        if (it == m_sections.cend())
        {
            SY_WARNF("[UiContextMenuService] Menu id='%s' references unregistered dynamic section '%s'",
                qPrintable(ownerId),
                qPrintable(sectionId));
            continue;
        }
        const int before = menu->actions().size();
        (*it)(menu);
        const int delta = menu->actions().size() - before;
        added += delta;
    }
    return added;
}


bool UiContextMenuService::hasConfigFor(const UiConfigData* config, const QString& contextMenuId)
{
    return findContextMenu(config, contextMenuId) != nullptr;
}

QMenu* UiContextMenuService::buildMenu(const UiConfigData* config,
    const QString& contextMenuId,
    IUiCommandDispatcher* dispatcher,
    QWidget* parent)
{
    const ContextMenuDef* def = findContextMenu(config, contextMenuId);
    if (!def)
    {
        // 未配置不是错误：调用方会回退到内建路径。用 DEBUG 级别避免刷日志，
        // 因为右键操作可能非常频繁。
        SY_DEBUGF("[UiContextMenuService] No config for context menu id='%s'", qPrintable(contextMenuId));
        return nullptr;
    }

    // 静态条目由 UiLayoutBuilder 构建：命令绑定、图标解析、授权门控与主菜单完全一致。
    // panelRegistry 传 nullptr —— 右键菜单不含 Dock/状态栏槽位。
    UiLayoutBuilder builder(nullptr, dispatcher, nullptr);
    QMenu* menu = builder.buildContextMenu(*def, parent);

    if (!menu)
    {
        // 授权不足或静态条目全被过滤。此时若仍有动态段可填，就新建一个空菜单继续；
        // 否则直接返回 nullptr。
        if (def->dynamicSections.isEmpty())
        {
            return nullptr;
        }
        menu = new QMenu(parent);
        menu->setObjectName(def->id);
    }

    // 追加动态段：与主菜单子菜单共用 fillDynamicSections，不在此另写一份追加逻辑
    fillDynamicSections(menu, def->dynamicSections, def->id);


    if (!hasRealAction(menu))
    {
        SY_DEBUGF("[UiContextMenuService] Context menu id='%s' resolved to empty, not shown", qPrintable(def->id));
        menu->deleteLater();
        return nullptr;
    }

    SY_DEBUGF("[UiContextMenuService] Context menu built from config id='%s' actions=%d dynamicSections=%d",
        qPrintable(def->id),
        static_cast<int>(menu->actions().size()),
        static_cast<int>(def->dynamicSections.size()));
    return menu;
}

void UiContextMenuService::resetForTest()
{
    m_sections.clear();
}
