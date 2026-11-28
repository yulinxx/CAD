#include "Model/Assembly/AssemblyPlanner.h"

#include <algorithm>
#include <functional>
#include <optional>
#include <utility>

#include <QHash>
#include <QSet>
#include <QtGlobal>

#include "UiShell/Contract/Command.h"
#include "UiShell/Contract/ITextProvider.h"

namespace AA
{
    namespace
    {
        bool featuresSatisfied(const QStringList& required, const FeatureFlagSet& flags)
        {
            for (const QString& key : required)
            {
                if (!flags.has(key))
                    return false;
            }
            return true;
        }

        // 排序.nOrder 相同时按 strId 排序
        bool paneAreaLess(const PaneAreaInfo& a, const PaneAreaInfo& b)
        {
            if (a.nOrder != b.nOrder)
                return a.nOrder < b.nOrder;
            return a.strId < b.strId;
        }

        bool panelLess(const PanelInfo& a, const PanelInfo& b)
        {
            if (a.nOrder != b.nOrder)
                return a.nOrder < b.nOrder;
            return a.strId < b.strId;
        }

        bool regionLess(const RegionInfo& a, const RegionInfo& b)
        {
            if (a.nOrder != b.nOrder)
                return a.nOrder < b.nOrder;
            return a.strId < b.strId;
        }

        class AssemblyPlanner final
        {
        public:
            AssemblyInfo build(const ProfileModel& model,
                const FeatureFlagSet& flags,
                const ICommandCatalog* catalog,
                const ITextProvider* text) const
            {
                AssemblyInfo info;

                const auto resolveText = [text](const QString& key) {
                    return text ? text->text(key) : key;
                    };

                QHash<QString, int> regionIndex;
                QHash<QString, QString> paneAreaRegion;
                QSet<QString> hiddenRegions;

                bool bTitleBarRegion = false;
                for (const RegionSpec& region : model.regions)
                {
                    if (region.defaultVisible && region.type == RegionType::TitleBar)
                    {
                        bTitleBarRegion = true;
                        break;
                    }
                }
                bTitleBarRegion = bTitleBarRegion && model.titleBar.bEnabled;

                // 标题栏模式由平台编译期固定：Windows 为自绘标题栏，
                // macOS/Linux 为原生标题栏。不从配置文件读取，也不作运行时推导。
#if defined(Q_OS_WIN)
                info.bNativeTitleBar = false;
#else
                info.bNativeTitleBar = true;
#endif
                // 无 TitleBar 区域时强制原生标题栏（无可绘制的自绘控件）。
                info.bNativeTitleBar = info.bNativeTitleBar || !bTitleBarRegion;

                // 菜单栏形态：默认窗口内菜单栏（菜单项图标可见、可用 QSS 皮肤）；
                // profile 显式写 bNativeMenuBar=true 时交给系统原生菜单栏
                // （macOS 上位于屏幕顶部，但原生菜单不画 QAction 图标）。
                info.bNativeMenuBar = model.window.nativeMenuBar.value_or(false);

                // 先建 area → region 的反向索引：面板只声明子区域 id，
                // 需要靠它判断面板所属区域是否存在、是否可见。
                for (const RegionSpec& region : model.regions)
                {
                    for (const PaneAreaSpec& area : region.paneAreas)
                        paneAreaRegion.insert(area.strId, region.strId);

                    if (!region.defaultVisible)
                        hiddenRegions.insert(region.strId);
                }

                // 1. 可见区域与空子区域
                for (const RegionSpec& region : model.regions)
                {
                    if (!region.defaultVisible)
                        continue;

                    // 原生标题栏模式下不生成自绘标题栏区域；菜单栏因此保持独立一行。
                    if (region.type == RegionType::TitleBar && info.bNativeTitleBar)
                        continue;

                    RegionInfo regionInfo;
                    regionInfo.strId = region.strId;
                    regionInfo.type = region.type;
                    regionInfo.nOrder = region.order;
                    regionInfo.bVisible = true;

                    for (const PaneAreaSpec& area : region.paneAreas)
                    {
                        PaneAreaInfo paneAreaInfo;
                        paneAreaInfo.strId = area.strId;
                        paneAreaInfo.nOrder = area.order;
                        paneAreaInfo.nStretch = area.stretch;
                        paneAreaInfo.nWidth = area.preferredWidth;
                        paneAreaInfo.nHeight = area.preferredHeight;
                        regionInfo.paneAreas.append(paneAreaInfo);
                    }
                    std::sort(regionInfo.paneAreas.begin(), regionInfo.paneAreas.end(), paneAreaLess);

                    regionIndex.insert(regionInfo.strId, static_cast<int>(info.regions.size()));
                    info.regions.append(regionInfo);
                }

                // 2. 面板落位（按功能开关与可见性过滤）
                for (const PanelSpec& panel : model.panels)
                {
                    if (!panel.defaultVisible)
                        continue;

                    const auto paneAreaIt = paneAreaRegion.constFind(panel.paneAreaId);
                    if (paneAreaIt == paneAreaRegion.constEnd())
                    {
                        // 子区域不存在只记警告不中止：一个面板配错不应导致整个界面无法装配。
                        info.strListWarnings.append(
                            QStringLiteral("panel '%1' references unknown area '%2'").arg(panel.strId, panel.paneAreaId));
                        continue;
                    }

                    // 区域被隐藏时，子面板都忽略
                    if (hiddenRegions.contains(*paneAreaIt))
                        continue;

                    // 开关不足的面板静默跳过，不产生警告：这是正常的按授权裁剪。
                    if (!featuresSatisfied(panel.requiredFeatures, flags))
                        continue;

                    const auto indexIt = regionIndex.constFind(*paneAreaIt);
                    if (indexIt == regionIndex.constEnd())
                        continue;

                    PanelInfo panelInfo;
                    panelInfo.strId = panel.strId;
                    panelInfo.factory = panel.factory;
                    panelInfo.paneAreaId = panel.paneAreaId;
                    panelInfo.nOrder = panel.order;
                    panelInfo.bVisible = true;
                    // 面板自身的期望尺寸：允许 profile 覆盖子区域默认布局，0 表示由子区域决定。
                    panelInfo.nWidth = panel.preferredWidth;
                    panelInfo.nHeight = panel.preferredHeight;
                    // 工厂私有参数原样透传：模型层不解释内容，缺字段时为空对象。
                    panelInfo.props = panel.props;
                    info.regions[*indexIt].panels.append(panelInfo);
                }

                for (RegionInfo& region : info.regions)
                    std::sort(region.panels.begin(), region.panels.end(), panelLess);

                std::sort(info.regions.begin(), info.regions.end(), regionLess);

                // 3. 标题栏描述（纯数据；控件由 titlebar 库独立构造）
                // 原生模式不生成自绘标题栏，present 保持 false，避免装配层误建控件；
                // 但品牌图标（窗口/任务栏用）无论哪种模式都需要，故在此统一解析。
                info.titleBar.strIcon = model.titleBar.strIcon;
                if (bTitleBarRegion && !info.bNativeTitleBar)
                {
                    TitleBarInfo& titleBar = info.titleBar;
                    titleBar.bPresent = true;
                    titleBar.strTitleKey = model.titleBar.strTitleKey.trimmed();
                    // 优先按文本 strId 解析；没有 strId 时用字面 title；两者都为空才回退 profileId。
                    if (!titleBar.strTitleKey.isEmpty())
                        titleBar.strTitle = resolveText(titleBar.strTitleKey).trimmed();
                    if (titleBar.strTitle.isEmpty())
                        titleBar.strTitle = model.titleBar.strTitle.trimmed();
                    if (titleBar.strTitle.isEmpty())
                        titleBar.strTitle = model.strProfileId;
                    titleBar.bShowTitle = model.titleBar.bShowTitle;
                    // 高度限制在 24~96：过小会压掉按钮，过大则与主界面争空间；非法值回到 36。
                    titleBar.nHeight = std::min(std::max(model.titleBar.nHeight <= 0 ? 36 : model.titleBar.nHeight, 24), 96);
                    // 拖拽与双击最大化恒可用，不再从 profile 读取开关。
                    titleBar.strBkgColor = model.titleBar.strBkgColor;
                    titleBar.strTextColor = model.titleBar.strTextColor;

                    // 标题栏控件只认识这三个语义按钮，未知 id 在此过滤，避免控件侧再兜底。
                    QSet<QString> knownButtons;
                    knownButtons.insert(QStringLiteral("minimize"));
                    knownButtons.insert(QStringLiteral("maximize"));
                    knownButtons.insert(QStringLiteral("close"));

                    QSet<QString> seenButtons;
                    for (const QString& button : model.titleBar.listButtons)
                    {
                        if (!knownButtons.contains(button))
                        {
                            info.strListWarnings.append(QStringLiteral("titleBar references unknown button '%1'").arg(button));
                            continue;
                        }
                        // 去重但保留首次出现的位置，重复声明不会画两个相同按钮。
                        if (seenButtons.contains(button))
                            continue;
                        seenButtons.insert(button);
                        titleBar.buttons.append(button);
                    }
                }

                // 最小尺寸同样做范围夹取，防止 profile 写出 0 或超大值破坏窗口行为。
                info.nMinWidth = std::min(std::max(model.window.minWidth <= 0 ? 800 : model.window.minWidth, 320), 10000);
                info.nMinHeight = std::min(std::max(model.window.minHeight <= 0 ? 600 : model.window.minHeight, 240), 10000);

                // 4. 菜单结构：递归构建 Command / Submenu / Separator
                // —— 节点递归构建 ——
                std::function<std::optional<MenuNodeInfo>(const MenuNodeSpec&, const QString&)> buildNode;
                buildNode = [&](const MenuNodeSpec& spec, const QString& contextMenuId) -> std::optional<MenuNodeInfo> {
                    MenuNodeInfo nodeInfo;
                    nodeInfo.type = static_cast<MenuNodeType>(static_cast<int>(spec.type));

                    if (spec.type == MenuNodeType::Command)
                    {
                        nodeInfo.strCmdId = spec.commandId;
                        nodeInfo.nOrder = spec.order;
                        nodeInfo.bCheckable = spec.checkable;
                        nodeInfo.strGroupKey = spec.groupKey;
                        nodeInfo.strTitleKey = spec.titleKey;
                        nodeInfo.strIconKey = spec.iconKey;

                        // 优先用 spec 声明的 strTitleKey/iconKey；缺省时从 catalog 的 CommandView 取。
                        if (catalog)
                        {
                            const CommandView* view = catalog->find(nodeInfo.strCmdId);
                            if (view)
                            {
                                if (nodeInfo.strTitleKey.isEmpty())
                                    nodeInfo.strTitleKey = view->strTitleKey;
                                if (nodeInfo.strIconKey.isEmpty())
                                    nodeInfo.strIconKey = view->strIconKey;
                                nodeInfo.requiredFeatures = view->listRequiredFeatures;
                            }
                            else
                            {
                                // catalog 存在但找不到该 command → 跳过（并警告）
                                info.strListWarnings.append(QStringLiteral("menu '%1' references unknown command '%2'")
                                    .arg(contextMenuId, nodeInfo.strCmdId));
                                return std::nullopt;
                            }
                        }

                        // strTitleKey 最终按语言包解析成 title。
                        nodeInfo.strTitle = resolveText(nodeInfo.strTitleKey);
                    }
                    else if (spec.type == MenuNodeType::Submenu)
                    {
                        nodeInfo.strId = spec.strId;
                        nodeInfo.strTitleKey = spec.titleKey;
                        nodeInfo.strIconKey = spec.iconKey;
                        nodeInfo.strTitle = resolveText(spec.titleKey);
                        nodeInfo.nOrder = spec.order;
                        for (const MenuNodeSpec& child : spec.children)
                        {
                            auto childInfo = buildNode(child, spec.strId);
                            if (childInfo.has_value())
                                nodeInfo.children.append(std::move(*childInfo));
                        }
                    }
                    else if (spec.type == MenuNodeType::Separator)
                    {
                        nodeInfo.nOrder = spec.order;
                    }
                    else if (spec.type == MenuNodeType::Builtin)
                    {
                        nodeInfo.strId = spec.strId;
                        nodeInfo.strIconKey = spec.iconKey;
                        nodeInfo.nOrder = spec.order;
                    }
                    return nodeInfo;
                    };

                for (const MenuNodeSpec& menu : model.menus)
                {
                    if (menu.strId.isEmpty() && menu.type != MenuNodeType::Separator)
                    {
                        info.strListWarnings.append(QStringLiteral("menu without id"));
                        continue;
                    }
                    auto menuInfo = buildNode(menu, menu.strId);
                    if (menuInfo.has_value())
                        info.menus.append(std::move(*menuInfo));
                }

                std::sort(info.menus.begin(), info.menus.end(), [](const MenuNodeInfo& a, const MenuNodeInfo& b) {
                    if (a.nOrder != b.nOrder)
                        return a.nOrder < b.nOrder;
                    return a.strId < b.strId;
                    });

                // 右键菜单用同一套节点构建与未知命令过滤规则，保证右键和主菜单状态一致。
                for (const MenuNodeSpec& menu : model.contextMenus)
                {
                    if (menu.strId.isEmpty() && menu.type != MenuNodeType::Separator)
                    {
                        info.strListWarnings.append(QStringLiteral("context menu without id"));
                        continue;
                    }
                    auto menuInfo = buildNode(menu, menu.strId);
                    if (!menuInfo.has_value())
                        continue;

                    std::sort(menuInfo->children.begin(), menuInfo->children.end(),
                        [](const MenuNodeInfo& a, const MenuNodeInfo& b) {
                        if (a.nOrder != b.nOrder)
                            return a.nOrder < b.nOrder;
                        return a.strCmdId < b.strCmdId;
                    });
                    info.contextMenus.insert(menu.strId, std::move(menuInfo->children));
                }

                // 内置 Help 扩展（语言/皮肤子菜单）移到 MenuController 处理：
                // 构建时能访问 services.translations，从 builtinLanguages() 动态填充。
                // 这里不再注入占位，避免硬编码语言数量。

                return info;
            }
        };
    }  // namespace

    AssemblyInfo planAssembly(const ProfileModel& model,
        const FeatureFlagSet& flags,
        const ICommandCatalog* catalog,
        const ITextProvider* text)
    {
        return AssemblyPlanner().build(model, flags, catalog, text);
    }
}  // namespace AA