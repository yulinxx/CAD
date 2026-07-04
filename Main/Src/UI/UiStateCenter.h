#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>

/**
 * @file UiStateCenter.h
 * @brief UI 状态中心定义
 *
 * 定义了 UI 状态中心类，负责管理和分发整个应用程序的 UI 状态。
 * 提供状态快照功能和信号通知机制。
 */

/**
 * @struct UiStateSnapshot
 * @brief UI 状态快照
 *
 * 封装了 UI 状态的所有关键信息，用于状态保存和恢复。
 */
struct UiStateSnapshot
{
    /// 当前工作台 ID
    QString currentWorkbenchId{ QStringLiteral("default") };
    /// 当前主题 ID
    QString currentThemeId{ QStringLiteral("system") };
    /// 当前视图模式
    QString currentViewMode{ QStringLiteral("none") };
    /// 当前图层 ID
    QString currentLayerId{ QStringLiteral("default") };
    /// 当前文档 ID
    QString currentDocumentId{ QStringLiteral("none") };
    /// 当前命令 ID
    QString currentCommandId{ QStringLiteral("idle") };
    /// 当前命令阶段
    QString currentCommandPhase{ QStringLiteral("idle") };
    /// 当前命令来源
    QString currentCommandOwner{ QStringLiteral("none") };
    /// 当前命令类型
    QString currentCommandType{ QStringLiteral("none") };
    /// 当前选择文本
    QString currentSelectionText;
    /// 当前选择来源
    QString currentSelectionSource{ QStringLiteral("none") };
    /// 当前选择类型
    QString currentSelectionType{ QStringLiteral("none") };
    /// 是否繁忙
    bool busy{ false };
    /// 是否有未保存更改
    bool dirty{ false };
    /// 元数据
    QVariantMap metadata;
};

/**
 * @class UiStateCenter
 * @brief UI 状态中心
 *
 * 集中管理所有 UI 状态，提供状态变更的信号通知机制。
 * 支持状态快照和各个状态属性的独立访问。
 */
class UiStateCenter final : public QObject
{
    Q_OBJECT

public:
    
    /// @param parent 父对象
    explicit UiStateCenter(QObject* parent = nullptr);

public:
    /// 获取状态快照
    UiStateSnapshot snapshot() const;

    /// 获取当前工作台 ID
    QString currentWorkbenchId() const;

    /// 获取当前主题 ID
    QString currentThemeId() const;

    /// 获取当前视图模式
    QString currentViewMode() const;

    /// 获取当前图层 ID
    QString currentLayerId() const;

    /// 获取当前文档 ID
    QString currentDocumentId() const;

    /// 获取当前命令 ID
    QString currentCommandId() const;

    /// 获取当前命令阶段
    QString currentCommandPhase() const;

    /// 获取当前选择文本
    QString currentSelectionText() const;

    /// 获取当前选择来源
    QString currentSelectionSource() const;

    /// 获取当前命令来源
    QString currentCommandOwner() const;

    /// 获取当前命令类型
    QString currentCommandType() const;

    /// 获取当前选择类型
    QString currentSelectionType() const;

    /// 是否繁忙
    bool busy() const;

    /// 是否有未保存更改
    bool dirty() const;

    /// 获取元数据
    QVariantMap metadata() const;

public slots:
    /// 设置当前工作台 ID
    /// @param id 工作台 ID
    void setCurrentWorkbenchId(const QString& id);

    /// 设置当前主题 ID
    /// @param id 主题 ID
    void setCurrentThemeId(const QString& id);

    /// 设置当前视图模式
    /// @param mode 视图模式
    void setCurrentViewMode(const QString& mode);

    /// 设置当前图层 ID
    /// @param layerId 图层 ID
    void setCurrentLayerId(const QString& layerId);

    /// 设置当前文档 ID
    /// @param documentId 文档 ID
    void setCurrentDocumentId(const QString& documentId);

    /// 设置当前命令 ID
    /// @param commandId 命令 ID
    void setCurrentCommandId(const QString& commandId);

    /// 设置当前命令阶段
    /// @param phase 命令阶段
    void setCurrentCommandPhase(const QString& phase);

    /// 设置当前命令来源
    /// @param owner 命令来源
    void setCurrentCommandOwner(const QString& owner);

    /// 设置当前命令类型
    /// @param type 命令类型
    void setCurrentCommandType(const QString& type);

    /// 设置当前选择文本
    /// @param text 选择文本
    void setCurrentSelectionText(const QString& text);

    /// 统一设置选择文本来源，便于 2D/3D 共用同一入口
    /// @param source 来源标识
    /// @param text 选择文本
    void setSelectionContext(const QString& source, const QString& text);

    /// 设置繁忙状态
    /// @param busy 是否繁忙
    void setBusy(bool busy);

    /// 设置脏状态
    /// @param dirty 是否有未保存更改
    void setDirty(bool dirty);

    /// 设置元数据
    /// @param metadata 元数据映射
    void setMetadata(const QVariantMap& metadata);

signals:
    /// 状态变更信号（所有状态变更都会触发）
    void stateChanged();

    /// 工作台变更信号
    void currentWorkbenchChanged(const QString& id);

    /// 主题变更信号
    void currentThemeChanged(const QString& id);

    /// 视图模式变更信号
    void currentViewModeChanged(const QString& mode);

    /// 图层变更信号
    void currentLayerChanged(const QString& layerId);

    /// 文档变更信号
    void currentDocumentChanged(const QString& documentId);

    /// 命令变更信号
    void currentCommandChanged(const QString& commandId);

    /// 命令阶段变更信号
    void currentCommandPhaseChanged(const QString& phase);

    /// 选择文本变更信号
    void currentSelectionTextChanged(const QString& text);

    /// 繁忙状态变更信号
    void busyChanged(bool busy);

    /// 脏状态变更信号
    void dirtyChanged(bool dirty);

    /// 元数据变更信号
    void metadataChanged();

private:
    /// 当前工作台 ID
    QString m_workbenchId{ QStringLiteral("default") };
    /// 当前主题 ID
    QString m_themeId{ QStringLiteral("system") };
    /// 当前视图模式
    QString m_viewMode{ QStringLiteral("none") };

    /// 当前图层 ID
    QString m_layerId{ QStringLiteral("default") };
    /// 当前文档 ID
    QString m_documentId{ QStringLiteral("none") };
    /// 当前命令 ID
    QString m_commandId{ QStringLiteral("idle") };

    /// 当前命令阶段
    QString m_commandPhase{ QStringLiteral("idle") };
    /// 当前命令来源
    QString m_commandOwner{ QStringLiteral("none") };
    /// 当前命令类型
    QString m_commandType{ QStringLiteral("none") };

    /// 当前选择文本
    QString m_selectionText;
    /// 当前选择来源
    QString m_selectionSource{ QStringLiteral("none") };
    /// 当前选择类型
    QString m_selectionType{ QStringLiteral("none") };

    /// 繁忙状态
    bool m_busy{ false };
    /// 脏状态
    bool m_dirty{ false };
    /// 元数据
    QVariantMap m_metadata;
};
