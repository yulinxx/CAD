#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include "Model/Spec/RegionType.h"
#include "UiShell/Contract/TitleBarSpec.h"
#include "Model/Spec/WindowSpec.h"

namespace AA
{
    // 子区域配置：profile 中声明的子区域原始数据，尚未做过滤与排序。
    struct PaneAreaSpec
    {
        QString strId;
        int order = 100;
        int stretch = 0;
        int preferredWidth = 0;
        int preferredHeight = 0;
    };

    // 面板配置：props 是工厂私有参数，模型层只保存不解释。
    struct PanelSpec
    {
        QString strId;
        QString factory;
        QString paneAreaId;
        int order = 100;
        bool defaultVisible = true;
        int preferredWidth = 0;
        int preferredHeight = 0;
        QStringList requiredFeatures;
        QJsonObject props;
    };

    // 区域配置：区域类型固定，客户只能配置 id/顺序/可见性与子区域。
    struct RegionSpec
    {
        QString strId;
        RegionType type = RegionType::TopBar;
        int order = 100;
        bool defaultVisible = true;
        QList<PaneAreaSpec> paneAreas;
    };

    // 菜单节点类型：决定节点是命令项、子菜单还是分隔符。
    enum class MenuNodeType
    {
        Command,   // 叶子节点：执行命令
        Submenu,   // 子菜单：包含 children
        Separator, // 分隔符：无字段、无子项
        Builtin    // 框架内置子菜单：id 为 BuiltinMenuId 之一，放在哪个菜单、什么位置由 profile 决定
    };

    // Builtin 菜单节点支持的 id。
    namespace BuiltinMenuId
    {
        inline constexpr const char* Language = "language";  // 语言切换子菜单（需要翻译服务）
        inline constexpr const char* Theme = "theme";        // 皮肤切换子菜单
    }  // namespace BuiltinMenuId

    // 菜单节点配置：递归结构，children 里可以嵌套 Submenu/Command/Separator。
    // type 字段必须显式指定（command / submenu / separator / builtin）。
    struct MenuNodeSpec
    {
        MenuNodeType type = MenuNodeType::Command;

        QString strId;         // Submenu 的唯一标识；Builtin 节点的内置菜单 id
        QString titleKey;   // Submenu/Command 的文本 id
        QString commandId;  // Command 节点：执行的命令 id
        QString iconKey;    // 可选图标 key
        int order = 100;

        // Command 节点属性
        bool checkable = false;
        QString groupKey;  // 勾选组：同组内的 checkable 命令互斥（例如语言组）

        QList<MenuNodeSpec> children;  // Submenu 的子项
    };

    // M5：品牌/外观元数据。全部可选，未配置时沿用客户代码与命令行设置。
    // 站点改外置 ui.json 即可换产品名、图标、默认皮肤与默认语言，无需重编。
    struct BrandSpec
    {
        QString strProduct;       // 窗口标题 / 关于框产品名
        QString strOrganization;  // 组织名（关于框 company）
        QString strIcon;          // 图标：qrc 路径或相对 profile 目录的路径
        QString strTheme;         // 默认皮肤 id（light / dark / 客户皮肤）
        QString strLanguage;      // 默认语言 id；显式 --lang 优先
        QString strCopyright;     // 关于框版权
        QString strWebsite;       // 关于框主页
        QString strEmail;         // 关于框支持邮箱
        QString strLicense;       // 关于框许可文本（可含换行）
        QString strLogo;          // 关于框 logo：qrc 或相对 profile 目录的路径
    };

    // profile 解析结果
    struct ProfileModel
    {
        int nVersion = 2;
        QString strProfileId;

        BrandSpec brand;

        QList<RegionSpec> regions;
        QList<PanelSpec> panels;
        QList<MenuNodeSpec> menus;
        QList<MenuNodeSpec> contextMenus;

        TitleBarSpec titleBar;
        WindowSpec window;
    };
}  // namespace AA
