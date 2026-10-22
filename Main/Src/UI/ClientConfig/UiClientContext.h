#pragma once

/**
 * @file UiClientContext.h
 * @brief 客户上下文：运行时唯一的「当前客户 ID」事实源
 *
 * 背景：
 *   历史实现存在两条互相独立的客户 ID 来源——
 *     - 菜单侧读运行时环境变量 SANYI_CLIENT_ID（WorkbenchMenuManager）
 *     - 布局侧读编译期宏 SANYI_CLIENT_ID（WorkbenchLayoutManager）
 *   两者可能解析出不同的客户，导致「菜单来自 A 配置、布局来自 B 配置」。
 *   同时编译期绑定客户 ID 意味着每个客户必须单独出一套二进制，
 *   与「一份安装包服务数十个客户」的交付目标冲突。
 *
 * 本类把客户 ID 收敛为**运行时单一事实源**，解析优先级由高到低：
 *   1. 显式覆盖         —— setClientIdOverride()，供命令行 --client=xxx 与单元测试使用
 *   2. 环境变量         —— SANYI_CLIENT_ID，供 CI / 现场排查临时切换
 *   3. 部署文件         —— <exe 目录>/client.id，交付包写入的唯一真源
 *   4. 用户设置         —— QSettings 键 Client/Id，兼容旧安装
 *   5. 内置默认         —— kDefaultClientId（san_yi）
 *
 * 配置查找顺序（外置优先，内置兜底）：
 *   1. 用户配置目录      —— AppConfigLocation/ui/<id>.json，供现场热修
 *   2. 交付包目录        —— <exe 目录>/configs/<id>.json
 *   3. 内置资源          —— :/configs/<id>.json
 *   4. 默认客户内置配置  —— :/configs/san_yi.json
 *
 * 解析结果会缓存，保证同一进程内菜单、工具栏、Dock、状态栏、右键菜单
 * 全部命中同一份配置资源。
 */

#include <QString>
#include <QStringList>

/// 客户上下文（进程级单例）
class UiClientContext
{
public:
    /// 内置默认客户 ID：所有客户配置的继承根
    static const char* const kDefaultClientId;

    /// 全局实例
    static UiClientContext& instance();

    /// 设置显式覆盖客户 ID（最高优先级）
    /// 传空字符串表示清除覆盖。调用后缓存失效，下次查询重新解析。
    /// @param clientId 客户 ID，例如 "client_a"
    void setClientIdOverride(const QString& clientId);

    /// 当前生效的客户 ID（带缓存）
    /// 首次调用时按优先级解析，并打印一条 INFO 日志说明来源，便于现场定位配置问题。
    const QString& clientId() const;

    /// 当前客户的 UI 配置资源路径，形如 ":/configs/client_a.json"
    /// 若该资源不存在，回退到默认客户配置并打印 WARN 日志——
    /// 避免因客户 ID 拼写错误导致整个 UI 构建失败（空窗口）。
    QString configResourcePath() const;

    /// 指定客户 ID 对应的配置路径（不做存在性回退，按外置优先顺序返回第一个可用路径）。
    /// 供加载器解析 extends 链与测试使用。
    static QString configResourcePathFor(const QString& clientId);

    /// 客户 ID 对应的全部候选配置路径（按优先级：用户目录 / exe 目录 / 内置资源）
    static QStringList configCandidatePaths(const QString& clientId);

    /// 读取 exe 同目录下的 client.id（首行，去 BOM 与空白）；不存在或为空返回空串
    static QString readClientIdFile(const QString& appDir = QString());

    /// 重置缓存（仅测试使用：切换环境变量后需要重新解析）
    void resetCacheForTest();

private:
    UiClientContext() = default;

    /// 按优先级解析客户 ID，同时输出来源描述用于日志
    static QString resolveClientId(QString& sourceOut);

    mutable QString m_cachedClientId;
    mutable bool m_resolved{ false };
    QString m_override;
};
