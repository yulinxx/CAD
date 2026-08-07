#pragma once

/**
 * @file UiPanelRegistry.h
 * @brief 面板工厂注册表
 *
 * 与 Docs/01-当前架构/UI定制变更设计方案.md 第 6.5 节对应。
 * 客户自定义面板通过工厂注册，JSON 配置中通过 widgetType 引用，
 * UiLayoutBuilder 构建 Dock 时通过 createPanel 创建实际 widget。
 */

#include <QMap>
#include <QString>
#include <QStringList>
#include <functional>

class QWidget;

using PanelFactory = std::function<QWidget*(QWidget* parent)>;

/// 面板工厂注册表
class UiPanelRegistry
{
public:
    /// 注册面板工厂
    /// @param id 面板类型 ID（对应 JSON 中的 widgetType）
    /// @param factory 面板创建工厂
    void registerPanel(const QString& id, PanelFactory factory);

    /// 创建面板 widget
    /// @param id 面板类型 ID
    /// @param parent 父对象
    /// @return 创建成功的 widget，未注册时返回 nullptr
    QWidget* createPanel(const QString& id, QWidget* parent);

    /// 面板是否已注册
    bool isPanelRegistered(const QString& id) const;

    /// 已注册面板 ID 列表
    QStringList registeredPanelIds() const;

private:
    QMap<QString, PanelFactory> m_factories;
};
