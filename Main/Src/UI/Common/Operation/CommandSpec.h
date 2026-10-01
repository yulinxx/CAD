/**
 * @file CommandSpec.h
 * @brief 共享命令规格表（2D/3D 双侧单一真相）
 *
 * 19 个跨工作台镜像命令在此声明一次（id/label/shortcut/icon/默认 surface），
 * 两侧 CommandCatalog 仅保留 OperationId 映射，消除重复维护。
 *
 * P1-4：2D/3D 命令镜像 19 对去重，see 架构审查报告。
 */
#pragma once

#include <QString>

namespace UI::Common
{
    struct CommandSpec
    {
        const char* id;           // 命令 id（如 "file.open"），JSON/menu/shortcut 统一键
        const char* label;        // 默认显示文本（英文源串，运行时经 tr 翻译）
        const char* shortcut;     // 默认快捷键（如 "Ctrl+O"），可为空
        const char* icon;         // 资源路径（:/ui/common/...），可为空
        const char* workbenches;  // 适用工作台（"2D,3D" 或 "2D" 或 "3D"），空=全部
        int surfaces;             // 默认 surface 位掩码（Menu/Toolbar/ContextMenu 等）
    };

    // === 19 条跨工作台镜像命令 ===
    inline constexpr CommandSpec kSharedCommands[] = {
        // File
        { "file.new",        "New",           "Ctrl+N",   nullptr,              "2D,3D",  0x1 }, // Menu
        { "file.open",       "Open",          "Ctrl+O",   ":/ui/common/Icons/File/open.svg",   "2D,3D",  0x1 },
        { "file.save",       "Save",          "Ctrl+S",   ":/ui/common/Icons/File/save.svg",   "2D,3D",  0x1 },
        { "file.save_as",    "Save As",       "Ctrl+Shift+S", nullptr,        "2D,3D",  0x1 },
        { "file.exit",       "Exit",          nullptr,    nullptr,              "2D,3D",  0x1 },

        // Edit
        { "edit.undo",       "Undo",          "Ctrl+Z",   ":/ui/common/Icons/Edit/undo.svg",   "2D,3D",  0x1 },
        { "edit.redo",       "Redo",          "Ctrl+Y",   ":/ui/common/Icons/Edit/redo.svg",   "2D,3D",  0x1 },
        { "edit.delete",     "Delete",        "Del",      ":/ui/common/Icons/Edit/delete.svg", "2D,3D",  0x1 },
        { "edit.select_all", "Select All",    "Ctrl+A",   nullptr,              "2D,3D",  0x1 },
        { "edit.invert_selection", "Invert Selection", nullptr, nullptr,   "2D,3D",  0x1 },
        { "edit.deselect",   "Deselect",      "Esc",      nullptr,              "2D,3D",  0x1 },
        { "edit.mirror",     "Mirror",        nullptr,    ":/ui/common/Icons/Edit/mirror.svg", "2D,3D",  0x1 },

        // View
        { "view.zoom_in",    "Zoom In",       "Ctrl++",   ":/ui/common/Icons/View/zoom_in.svg",  "2D,3D",  0x3 }, // Menu|Toolbar
        { "view.zoom_out",   "Zoom Out",      "Ctrl+-",   ":/ui/common/Icons/View/zoom_out.svg", "2D,3D",  0x3 },
        { "view.reset",      "Reset View",    "Ctrl+0",   ":/ui/common/Icons/View/reset.svg",    "2D,3D",  0x3 },
        { "view.capture",    "Capture View",  "F12",      ":/ui/common/Icons/View/capture.svg",  "2D,3D",  0x1 },

        // Help
        { "help.docs",       "Documentation", "F1",       ":/ui/common/Icons/Help/docs.svg",     "2D,3D",  0x1 },
        { "help.settings",   "Settings",      "Ctrl+,",   ":/ui/common/Icons/Help/settings.svg", "2D,3D",  0x1 },
        { "help.about",      "About",         nullptr,    ":/ui/common/Icons/Help/about.svg",    "2D,3D",  0x1 },
        { "help.shortcuts",  "Shortcuts",     nullptr,    ":/ui/common/Icons/Help/shortcuts.svg","2D,3D",  0x1 },
    };

    inline constexpr int kSharedCommandCount = sizeof(kSharedCommands) / sizeof(CommandSpec);

    // 辅助：按 id 查找
    inline const CommandSpec* findSharedCommand(const QString& id)
    {
        for (int i = 0; i < kSharedCommandCount; ++i)
        {
            if (id == QLatin1String(kSharedCommands[i].id))
            {
                return &kSharedCommands[i];
            }
        }
        return nullptr;
    }
} // namespace UI::Common