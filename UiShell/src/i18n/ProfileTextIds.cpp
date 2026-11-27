// 本文件不参与运行逻辑，只给 lupdate 提供 profile 里的稳定文本 id。
//
// profile 是 JSON，lupdate 不会扫描；用 QT_TRANSLATE_NOOP 把 id 和期望的
// context 显式登记下来，新增 profile 文本时同步在这里补一行，再执行 README
// 中的 lupdate 命令即可。
//
// context 规则与 textContextForKey() 一致：id 第一个 '.' 之前的部分。
#include <QCoreApplication>

namespace
{
    [[maybe_unused]] const char* const kProfileTextIds[] = {
        // 客户标题（context: mantisoft / tyvokstudio / goodlaser）
        QT_TRANSLATE_NOOP("mantisoft", "mantisoft.title"),
        QT_TRANSLATE_NOOP("tyvokstudio", "tyvokstudio.title"),
        QT_TRANSLATE_NOOP("goodlaser", "goodlaser.title"),

        // 菜单（context: menu）
        QT_TRANSLATE_NOOP("menu", "menu.file"),
        QT_TRANSLATE_NOOP("menu", "menu.edit"),
        QT_TRANSLATE_NOOP("menu", "menu.help"),
        QT_TRANSLATE_NOOP("menu", "menu.project"),
        QT_TRANSLATE_NOOP("menu", "menu.machining"),
        QT_TRANSLATE_NOOP("menu", "menu.tools"),
        QT_TRANSLATE_NOOP("menu", "menu.design"),
        QT_TRANSLATE_NOOP("menu", "menu.manufacture"),
        QT_TRANSLATE_NOOP("menu", "menu.analysis"),
        QT_TRANSLATE_NOOP("menu", "menu.window"),

        // 命令（context: command）
        QT_TRANSLATE_NOOP("command", "command.file.new"),
        QT_TRANSLATE_NOOP("command", "command.file.open"),
        QT_TRANSLATE_NOOP("command", "command.file.save"),
        QT_TRANSLATE_NOOP("command", "command.edit.undo"),
        QT_TRANSLATE_NOOP("command", "command.edit.redo"),
        QT_TRANSLATE_NOOP("command", "command.help.about"),
        QT_TRANSLATE_NOOP("command", "command.project.new"),
        QT_TRANSLATE_NOOP("command", "command.project.open"),
        QT_TRANSLATE_NOOP("command", "command.machining.start"),
        QT_TRANSLATE_NOOP("command", "command.machining.pause"),
        QT_TRANSLATE_NOOP("command", "command.tools.settings"),
        QT_TRANSLATE_NOOP("command", "command.design.new"),
        QT_TRANSLATE_NOOP("command", "command.design.sketch"),
        QT_TRANSLATE_NOOP("command", "command.manufacture.preview"),
        QT_TRANSLATE_NOOP("command", "command.manufacture.run"),
        QT_TRANSLATE_NOOP("command", "command.analysis.measure"),
        QT_TRANSLATE_NOOP("command", "command.window.reset"),
        QT_TRANSLATE_NOOP("command", "command.sample.run"),

        // 关于对话框（context: about）
        QT_TRANSLATE_NOOP("about", "about.mantisoft.product"),
        QT_TRANSLATE_NOOP("about", "about.mantisoft.company"),
        QT_TRANSLATE_NOOP("about", "about.mantisoft.copyright"),
        QT_TRANSLATE_NOOP("about", "about.tyvokstudio.product"),
        QT_TRANSLATE_NOOP("about", "about.tyvokstudio.company"),
        QT_TRANSLATE_NOOP("about", "about.tyvokstudio.copyright"),
        QT_TRANSLATE_NOOP("about", "about.tyvokstudio.license"),
        QT_TRANSLATE_NOOP("about", "about.goodlaser.product"),
        QT_TRANSLATE_NOOP("about", "about.goodlaser.company"),
        QT_TRANSLATE_NOOP("about", "about.goodlaser.copyright"),
    };
}  // namespace