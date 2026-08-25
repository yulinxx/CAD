#pragma once

/**
 * @file UiFeatureGate.h
 * @brief 功能授权闸门：把 License 的 features 字段接到 UI 配置的 feature 字段上
 *
 * 背景（P0-3）：
 *   注册码载荷（LicenseInfo.features）与 UI 配置（MenuActionDef::feature /
 *   ToolBarActionDef::feature / ToolBarDef::feature）两端**都已存在** feature 字段，
 *   且配置侧已被 UiConfigLoader 正确解析，但中间缺少「消费」这一环——
 *   授权功能集从未被读入内存，也没有任何 UI 元素查询过它。
 *   结果是 License 退化为「能不能启动」的二元开关，无法按客户授权粒度开关功能。
 *
 * 本类补上这一环：
 *   写入侧：启动许可校验通过后（CADApplicationRuntime）、以及注册码激活成功后
 *           （LicenseDialog），把 LicenseInfo.features 解析进来。
 *   读取侧：UiLayoutBuilder 构建菜单/工具栏项时、WorkbenchMenuManager 过滤菜单时，
 *           调用 isAllowed(feature) 决定该项是否出现。
 *
 * 语义约定：
 *   - featureId 为空       → 放行（绝大多数基础功能不做授权限制）
 *   - unrestricted 模式    → 放行全部。许可校验未启用（未定义 SANYI_ENABLE_LICENSE）
 *                            时的默认状态，保证开发构建不被授权拦住。
 *   - 授权集已加载         → 仅放行集合内的 featureId
 *
 * features 字段格式：逗号/分号/空格分隔的功能 ID 列表，例如
 *   "nesting,vision,relief3d"
 * 通配符 "*" 或 "all" 表示全功能授权。
 */

#include <QSet>
#include <QString>
#include <QStringList>

/// 功能授权闸门（进程级单例）
class UiFeatureGate
{
public:
    /// 全局实例
    static UiFeatureGate& instance();

    /// 从 License 的 features 原始字符串加载授权集
    /// @param featureCsv LicenseInfo.features 的内容，支持 , ; 空格 分隔
    void loadFromLicenseString(const QString& featureCsv);

    /// 直接设置授权功能集（供测试与非 License 来源使用）
    void setLicensedFeatures(const QStringList& features);

    /// 设置无限制模式：放行所有 feature 查询
    /// 许可校验未启用时应设为 true，避免开发构建被授权闸门挡住全部功能。
    void setUnrestricted(bool unrestricted);

    /// 是否处于无限制模式
    bool isUnrestricted() const
    {
        return m_unrestricted;
    }

    /// 判断某功能是否被授权
    /// @param featureId UI 配置中的 feature 字段；空字符串表示无授权限制，直接放行
    bool isAllowed(const QString& featureId) const;

    /// 当前授权功能集（排序后，便于日志与测试断言）
    QStringList licensedFeatures() const;

    /// 清空授权集并回到受限模式（仅测试使用）
    void resetForTest();

private:
    UiFeatureGate() = default;

    /// 授权功能 ID 集合，统一小写存储以避免大小写不一致导致误拦
    QSet<QString> m_features;
    /// 无限制模式：默认 true，直到许可校验明确开启并写入授权集
    bool m_unrestricted{ true };
};
