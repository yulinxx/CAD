/**
 * @file MacMenuCleanup.mm
 * @brief macOS 原生菜单系统项清理实现
 *
 * 通过 Objective-C++ 直接操作 NSMenu，移除 macOS 自动注入
 * 到 Edit 菜单的系统菜单项（表情与符号、开始听写等），
 * 保留原生顶部菜单栏体验。
 *
 * 本文件只在 APPLE 平台参与编译（见 Main/CMakeLists.txt 的 if(APPLE) 分支），
 * 非 macOS 平台由头文件提供内联空实现，这里不需要再做平台条件编译。
 */
#include "Platform/MacMenuCleanup.h"

#include "Log/SyLogger.h"

#import <Cocoa/Cocoa.h>


/// 判断 NSMenuItem 是否为 macOS 系统注入项
static BOOL isSystemInjectedItem(NSMenuItem* item)
{
    SEL action = [item action];

    // orderFrontCharacterPalette: → "表情与符号" / "Emoji & Symbols"
    if (action == @selector(orderFrontCharacterPalette:))
    {
        return YES;
    }

    // startDictation: → "开始听写" / "Start Dictation"
    if (action == @selector(startDictation:))
    {
        return YES;
    }

    return NO;
}

/// 递归在 NSMenu 中查找标题匹配的子菜单
static NSMenu* findSubMenuByTitle(NSMenu* menu, NSString* title)
{
    for (NSMenuItem* item in [menu itemArray])
    {
        NSMenu* submenu = [item submenu];
        if (submenu && [[submenu title] isEqualToString:title])
        {
            return submenu;
        }
    }
    return nil;
}

/// 递归设置菜单树中所有项的图片可见性（macOS 27+）。
/// 注意：不对 [item image] 做条件判断 —— Qt 把 QIcon 同步到 NSMenuItem.image 是
/// 延迟的，扫描时 image 可能还没挂上；只对"当前有图"的项设置会漏掉这些项，
/// 表现为图标时有时无。对无图项设置可见性是无害的。
static void setVisibleRecursively(NSMenu* menu)
{
    for (NSMenuItem* item in [menu itemArray])
    {
        if (@available(macOS 27.0, *))
        {
            item.preferredImageVisibility = NSMenuItemImageVisibilityVisible;
        }
        if ([item submenu])
        {
            setVisibleRecursively([item submenu]);
        }
    }
}

/// 递归统计仍会被 AppKit 隐藏的菜单图标，并收集前几个示例路径。
static int countHiddenImageItems(NSMenu* menu, NSString* parentPath, NSMutableArray<NSString*>* samples)
{
    int hidden = 0;
    for (NSMenuItem* item in [menu itemArray])
    {
        NSString* itemPath = parentPath.length > 0
            ? [NSString stringWithFormat:@"%@ > %@", parentPath, [item title]]
            : [item title];

        if (@available(macOS 27.0, *))
        {
            // 只有带 QIcon 同步过来的图片、但可见性不是 Visible 的项才会被隐藏。
            if ([item image] && [item preferredImageVisibility] != NSMenuItemImageVisibilityVisible)
            {
                ++hidden;
                if ([samples count] < 5)
                {
                    [samples addObject:itemPath];
                }
            }
        }

        NSMenu* submenu = [item submenu];
        if (submenu)
        {
            hidden += countHiddenImageItems(submenu, itemPath, samples);
        }
    }
    return hidden;
}

bool checkMacMenuImagesVisible()
{
    if (@available(macOS 27.0, *))
    {
        NSMenu* mainMenu = [NSApp mainMenu];
        if (!mainMenu)
        {
            // 菜单尚未同步到 NSMenu，等下一次菜单变更或自检再看。
            return true;
        }

        NSMutableArray<NSString*>* samples = [NSMutableArray array];
        const int hidden = countHiddenImageItems(mainMenu, @"", samples);

        // 同一批隐藏项只报一次，避免菜单变更通知风暴刷屏；问题消失后再次出现会重新报。
        static int s_lastReportedHiddenCount = 0;
        if (hidden > 0)
        {
            if (hidden != s_lastReportedHiddenCount)
            {
                s_lastReportedHiddenCount = hidden;
                NSString* sampleText = [samples componentsJoinedByString:@", "];
                SY_ERRORF("[MacMenu] %d menu item(s) with icons are hidden by AppKit "
                          "(preferredImageVisibility != Visible) on macOS %s. Menu icons will be missing. "
                          "Sample: %s. Check ensureMacMenuImagesVisible() and Qt menu icon sync.",
                    hidden,
                    [[[NSProcessInfo processInfo] operatingSystemVersionString] UTF8String],
                    [sampleText UTF8String]);
            }
            return false;
        }

        s_lastReportedHiddenCount = 0;
    }
    return true;
}

// ---- 菜单变更监听 ----
// Qt 在启动、菜单重建、动态区块（最近文件等）填充时都会增量同步 NSMenu，
// 任何时点都可能插入"可见性仍为 Automatic"的新 NSMenuItem。
// 只在重建后扫一次无法覆盖所有时序，这里监听菜单变更通知自动补设置。

/// 防递归：我们自己设置 preferredImageVisibility 也可能触发变更通知
static BOOL g_applyingVisibility = NO;
/// singleShot 合并标记：把同一事件循环轮内的通知风暴合并为一次全树扫描
static BOOL g_rescanScheduled = NO;
/// 监听器只装一次
static BOOL g_menuChangeObserverInstalled = NO;

static void applyImageVisibilityToMainMenu()
{
    NSMenu* mainMenu = [NSApp mainMenu];
    if (!mainMenu)
    {
        return;
    }
    g_applyingVisibility = YES;
    setVisibleRecursively(mainMenu);
    g_applyingVisibility = NO;

    // 设置完立即自检：如果未来 AppKit 再次改变行为、或属性设置被忽略，
    // 这里会留下错误日志，而不是又变成"图标悄悄消失、没人知道"。
    checkMacMenuImagesVisible();
}

/// NSMenuDidChangeItemNotification 回调：菜单项增删/变更后重新应用可见性。
/// object 为发生变化的 NSMenu（含 Qt 延迟同步、动态区块填充等一切来源），
/// 这里不做对象过滤、直接对主菜单树全量重扫 —— 菜单树很小，开销可忽略。
static void handleMenuDidChangeItems(NSNotification* note)
{
    if (g_applyingVisibility || ![note.name isEqualToString:NSMenuDidChangeItemNotification])
    {
        return;
    }
    if (g_rescanScheduled)
    {
        return;
    }
    g_rescanScheduled = YES;
    // 延后到本轮通知风暴结束后统一扫一次
    dispatch_async(dispatch_get_main_queue(), ^
    {
        g_rescanScheduled = NO;
        if (@available(macOS 27.0, *))
        {
            applyImageVisibilityToMainMenu();
        }
    });
}

void ensureMacMenuImagesVisible()
{
    // macOS 27 起 AppKit 默认隐藏菜单项图片（preferredImageVisibility 默认 Automatic），
    // Qt 不感知这个新开关 —— 图标同步正常却不显示。这里显式声明"始终显示"。
    if (@available(macOS 27.0, *))
    {
        applyImageVisibilityToMainMenu();

        // 首次调用时装监听器，此后菜单树任何变化都会自动补设置，
        // 不再依赖调用点记得在每次重建后调用本函数。
        if (!g_menuChangeObserverInstalled)
        {
            g_menuChangeObserverInstalled = YES;
            [[NSNotificationCenter defaultCenter] addObserverForName:NSMenuDidChangeItemNotification
                                                             object:nil
                                                              queue:NSOperationQueue.mainQueue
                                                         usingBlock:^(NSNotification* notification) {
                                                             handleMenuDidChangeItems(notification);
                                                         }];
        }
    }
}

void cleanupMacEditMenuSystemItems()
{
    NSMenu* mainMenu = [NSApp mainMenu];
    if (!mainMenu)
    {
        return;
    }

    // Edit 菜单的标题可能因系统语言而异，常见值：
    // English: "Edit", Chinese: "编辑", Japanese: "編集"
    NSArray* editTitles = @[
        @"Edit",
        @"编辑",
        @"編集",
        @"Bearbeiten",
        @"Édition",
        @"Modifica"
    ];

    for (NSString* title in editTitles)
    {
        NSMenu* editMenu = findSubMenuByTitle(mainMenu, title);
        if (!editMenu)
        {
            continue;
        }

        // 从末尾向前遍历，移除系统注入项及其前面的分隔线
        NSArray* items = [editMenu itemArray];
        for (NSInteger i = [items count] - 1; i >= 0; --i)
        {
            NSMenuItem* item = items[i];

            if (isSystemInjectedItem(item))
            {
                [editMenu removeItem:item];
                continue;
            }

            // 如果该项是分隔线且其后（已处理的，即更靠后的）原有系统项被移除，
            // 则检查此分隔线是否变成末尾多余分隔线
            if ([item isSeparatorItem])
            {
                // 检查分隔线之后是否还有非分隔线项
                BOOL hasItemAfter = NO;
                for (NSInteger j = i + 1; j < (NSInteger)[items count]; ++j)
                {
                    NSMenuItem* afterItem = items[j];
                    if (![afterItem isSeparatorItem] && !isSystemInjectedItem(afterItem))
                    {
                        hasItemAfter = YES;
                        break;
                    }
                }
                if (!hasItemAfter)
                {
                    [editMenu removeItem:item];
                }
            }
        }

        // 只处理第一个匹配的 Edit 菜单
        break;
    }
}

