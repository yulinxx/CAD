#pragma once

/**
 * @file UiClientContext.h
 * @brief 客户上下文：运行时唯一的「当前客户 ID」事实源
 *
 * 背景（P0-1）：
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
 *   3. 用户设置         —— QSettings 键 Client/Id，供安装包写入客户标识
 *   4. 内置默认         —— kDefaultClientId（san_yi）
 *
 * 解析结果会缓存，保证同一进程内菜单、工具栏、Dock、状态栏、右键菜单
 * 全部命中同一份配置资源。
 */

#include <QString>

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

    /// 指定客户 ID 对应的配置资源路径（不做存在性回退，供加载器解析 extends 链使用）
    static QString configResourcePathFor(const QString& clientId);

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
