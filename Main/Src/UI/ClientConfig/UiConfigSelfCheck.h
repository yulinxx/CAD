#pragma once

/**
 * @file UiConfigSelfCheck.h
 * @brief 客户配置可信性启动自检：CMake 开关 / JSON 配置 / License 授权 三侧交叉核对
 *
 * 要解决的问题：这三侧各自都"看起来正常"，但两两不一致时全都是静默失效——
 *   1. JSON 写了命令、目录里却没这个命令
 *      → UiLayoutBuilder::bindAction 不接 lambda，按钮照常显示但永久点不动；
 *   2. JSON 标了 feature、License 也放行，但对应模块没编译
 *      → 点了没反应，且一条日志都没有（最坑：功能"卖出去了"却不存在）；
 *   3. JSON 声明了 3D 工作台，构建却没开 BUILD_UI3D
 *      → 菜单项凭空消失，看不出是配置写错还是构建裁掉了。
 * 这三类问题历史上都靠人工翻 JSON 才发现。本自检在启动时一次性报出来。
 *
 * 只报告、不修改任何状态：自检发现问题也不阻止启动（生产现场宁可少个按钮也要能开机），
 * 但会把结论写进日志，出问题时第一时间能定位到是三侧里哪一侧配错了。
 *
 * 命令引用的遍历逻辑同时被配置契约测试复用（CommandUiWiringTests），
 * 避免"产品代码一套遍历、测试再抄一套"导致两边判定漂移。
 */

#include <QString>
#include <QStringList>
#include <QVector>

struct UiConfigData;

/// 配置里的一处命令引用
struct UiConfigCommandRef
{
    QString commandId;
    /// 定位路径，形如 "menus/edit/edit.transform/edit.move"，报错时用于直接翻 JSON
    QString path;
    /// 生效工作台；为空表示 JSON 未声明（按"两侧都得能解析"要求）
    QStringList workbenches;
};

/// 配置里的一处 feature 引用
struct UiConfigFeatureRef
{
    QString featureId;
    QString path;
};

/// 自检结论
struct UiConfigSelfCheckReport
{
    /// 命令 id 在所属工作台的命令目录里解析不出来（按钮会永久点不动）
    QStringList unresolvedCommands;
    /// 命令在目录里缺失，但缺失原因是对应模块没编译（属预期裁剪，不是接线漏洞）
    QStringList gatedOutByBuild;

    /// JSON 引用 + License 放行，但模块未编译（功能不存在却被放行）
    QStringList licensedButNotCompiled;
    /// JSON 引用 + 模块已编译，但 License 未放行（正常的授权限制，仅记录）
    QStringList compiledButNotLicensed;
    /// JSON 声明了某工作台，但对应构建开关未启用
    QStringList workbenchNotCompiled;

    int checkedCommandCount{ 0 };
    int checkedFeatureCount{ 0 };

    /// 是否存在"必须有人处理"的问题（命令解析不出 / 放行了没编译的功能）
    bool hasBlockingIssue() const
    {
        return !unresolvedCommands.isEmpty() || !licensedButNotCompiled.isEmpty();
    }
};

/// 客户配置自检（无状态，全静态）
class UiConfigSelfCheck
{
public:
    /// 收集配置里全部命令引用（菜单 + 工具栏 + 右键菜单 + 快捷键）
    static QVector<UiConfigCommandRef> collectCommandRefs(const UiConfigData& config);

    /// 收集配置里全部 feature 引用（菜单项 / 工具栏项 / 工具栏 / 右键菜单 / 状态栏槽位）
    static QVector<UiConfigFeatureRef> collectFeatureRefs(const UiConfigData& config);

    /// featureId 对应的模块是否编进了这份构建
    /// @param known 输出：该 featureId 是否有已知的编译开关映射；false 时返回值无意义
    static bool isFeatureCompiledIn(const QString& featureId, bool& known);

    /// 执行自检
    static UiConfigSelfCheckReport run(const UiConfigData& config);

    /// 把结论写进日志（一次性汇总，不逐条刷屏）
    static void logReport(const UiConfigSelfCheckReport& report, const QString& clientId);

    /// 加载当前客户配置并自检 + 落日志。启动路径的唯一入口。
    static void runAndLogForCurrentClient();
};
