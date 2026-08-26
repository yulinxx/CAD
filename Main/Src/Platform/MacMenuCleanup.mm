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
                for (NSInteger j = i + 1; j < [items count]; ++j)
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

