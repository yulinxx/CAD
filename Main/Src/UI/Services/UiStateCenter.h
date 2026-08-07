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
    /// 图层可见性
    bool layerVisible{ true };
    /// 图层锁定状态
    bool layerLocked{ false };
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
    /// 当前交互事件类型（commandBegin / mouseDown / mouseMove / mouseUp / key）
    QString interactionKind;
    /// 当前交互事件指针 X
    int interactionPointerX{ -1 };
    /// 当前交互事件指针 Y
    int interactionPointerY{ -1 };
    /// 当前交互事件按键
    int interactionKey{ -1 };
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
    /// 命令是否最近失败了（UI 回退的判定依据）
    bool commandFailed{ false };
    /// 失败的命令 ID
    QString failedCommandId;
    /// 失败原因描述
    QString failureMessage;
    /// 当前任务进度 (0-100)，-1 表示无进行中的任务
    int progress{ -1 };
    /// 当前状态消息（用于状态栏/进度提示）
    QString statusMessage;
    /// 当前状态提示（用于命令引导、导入导出提示等展示文本）
    QString statusPrompt;
    /// 当前任务阶段标识（如 "parsing", "building", "applying", "writing"）
    QString taskPhase;
    /// 最近一次错误码（0 表示无错误）
    int errorCode{ 0 };
    /// 元数据
    QVariantMap metadata;
    /// 渲染刷新状态（"idle", "incremental", "full", "pending"）
    QString refreshState{ QStringLiteral("idle") };
    /// 当前激活工具 ID（工作台切换时恢复工具状态）
    QString activeToolId;
    /// 当前输入焦点控件名称（工作台切换时恢复焦点）
    QString inputFocusWidget;
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

    /// 获取当前交互事件类型
    QString interactionKind() const;

    /// 获取当前交互事件指针 X
    int interactionPointerX() const;

    /// 获取当前交互事件指针 Y
    int interactionPointerY() const;

    /// 获取当前交互事件按键
    int interactionKey() const;

    /// 获取当前选择类型
    QString currentSelectionType() const;

    /// 是否繁忙
    bool busy() const;

    /// 是否有未保存更改
    bool dirty() const;

    /// 获取当前任务进度 (-1 表示无任务)
    int progress() const;

    /// 获取当前状态消息
    QString statusMessage() const;

    /// 获取当前状态提示
    QString statusPrompt() const;

    /// 获取当前任务阶段
    QString taskPhase() const;

    /// 获取最近错误码
    int errorCode() const;

    /// 获取渲染刷新状态
    QString refreshState() const;

    /// 获取当前激活工具 ID
    QString activeToolId() const;

    /// 获取当前输入焦点控件名称
    QString inputFocusWidget() const;

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

    /// 设置图层可见性状态
    void setLayerVisibilityState(bool visible);
    /// 返回图层可见性状态
    bool layerVisibilityState() const;

    /// 设置图层锁定状态
    void setLayerLockState(bool locked);
    /// 返回图层锁定状态
    bool layerLockState() const;

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

    /// 设置当前交互事件状态
    /// @param kind 事件类型
    /// @param pointerX 指针 X
    /// @param pointerY 指针 Y
    /// @param key 按键码
    void setInteractionState(const QString& kind, int pointerX = -1, int pointerY = -1, int key = -1);

    /// 清除当前交互事件状态
    void clearInteractionState();

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

    /// 统一设置命令失败状态（触发 UI 回退）
    /// @param commandId 失败的命令 ID
    /// @param message 失败原因
    void setCommandFailed(const QString& commandId, const QString& message);

    /// 清除命令失败状态
    void clearCommandFailed();

    /// 设置渲染刷新状态
    /// @param state 刷新状态标识（"idle", "incremental", "full", "pending"）
    void setRefreshState(const QString& state);

    /// 设置元数据
    /// @param metadata 元数据映射
    void setMetadata(const QVariantMap& metadata);

    /// 设置当前状态提示
    /// @param prompt 状态提示内容
    void setStatusPrompt(const QString& prompt);

    /// 统一设置任务进度和消息
    /// @param progress 进度值 (0-100)，-1 表示清除进度
    /// @param message 状态消息
    void setProgress(int progress, const QString& message);

    /// 设置任务阶段和消息（用于导入/导出/保存等阶段性任务）
    /// @param phase 阶段标识（如 "parsing", "building", "writing"）
    /// @param message 阶段描述消息
    void setTaskPhase(const QString& phase, const QString& message);

    /// 设置错误状态（统一错误通知入口）
    /// @param code 错误码
    /// @param message 错误描述
    void setError(int code, const QString& message);

    /// 清除错误状态
    void clearError();

    /// 清除任务进度和阶段（任务完成时调用）
    void clearTask();

    /// 设置当前激活工具 ID
    /// @param toolId 工具 ID
    void setActiveToolId(const QString& toolId);

    /// 设置当前输入焦点控件名称
    /// @param widgetName 控件 objectName
    void setInputFocusWidget(const QString& widgetName);

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

    /// 图层可见性变更信号
    void layerVisibilityChanged(bool visible);
    /// 图层锁定状态变更信号
    void layerLockChanged(bool locked);

    /// 文档变更信号
    void currentDocumentChanged(const QString& documentId);

    /// 命令变更信号
    void currentCommandChanged(const QString& commandId);

    /// 命令阶段变更信号
    void currentCommandPhaseChanged(const QString& phase);

    /// 交互事件状态变更信号
    void interactionStateChanged(const QString& kind, int pointerX, int pointerY, int key);

    /// 选择文本变更信号
    void currentSelectionTextChanged(const QString& text);

    /// 繁忙状态变更信号
    void busyChanged(bool busy);

    /// 脏状态变更信号
    void dirtyChanged(bool dirty);

    /// 命令失败信号（UI 回退的触发入口）
    void commandFailed(const QString& commandId, const QString& message);

    /// 渲染刷新状态变更信号
    void refreshStateChanged(const QString& state);

    /// 状态提示变更信号
    void statusPromptChanged(const QString& prompt);

    /// 元数据变更信号
    void metadataChanged();

    /// 任务进度变更信号
    /// @param progress 进度值 (0-100)，-1 表示无任务
    /// @param message 状态消息
    void progressChanged(int progress, const QString& message);

    /// 任务阶段变更信号
    /// @param phase 阶段标识
    /// @param message 阶段描述
    void taskPhaseChanged(const QString& phase, const QString& message);

    /// 错误状态变更信号
    /// @param code 错误码
    /// @param message 错误描述
    void errorOccurred(int code, const QString& message);

    /// 激活工具变更信号
    /// @param toolId 工具 ID
    void activeToolChanged(const QString& toolId);

    /// 输入焦点控件变更信号
    /// @param widgetName 控件名称
    void inputFocusWidgetChanged(const QString& widgetName);

private:
    /// 当前工作台 ID
    QString m_workbenchId{ QStringLiteral("default") };
    /// 当前主题 ID
    QString m_themeId{ QStringLiteral("system") };
    /// 当前视图模式
    QString m_viewMode{ QStringLiteral("none") };

    /// 当前图层 ID
    QString m_layerId{ QStringLiteral("default") };
    /// 图层可见性状态
    bool m_layerVisible{ true };
    /// 图层锁定状态
    bool m_layerLocked{ false };
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
    /// 当前交互事件类型
    QString m_interactionKind;
    /// 当前交互事件指针 X
    int m_interactionPointerX{ -1 };
    /// 当前交互事件指针 Y
    int m_interactionPointerY{ -1 };
    /// 当前交互事件按键
    int m_interactionKey{ -1 };

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
    /// 命令是否处于失败状态（触发 UI 回退的标识）
    bool m_commandFailed{ false };
    /// 失败的命令 ID
    QString m_failedCommandId;
    /// 失败原因描述
    QString m_failureMessage;
    /// 当前任务进度 (0-100)，-1 表示无进行中的任务
    int m_progress{ -1 };
    /// 当前状态消息
    QString m_statusMessage;
    /// 当前状态提示
    QString m_statusPrompt;
    /// 当前任务阶段
    QString m_taskPhase;
    /// 最近错误码
    int m_errorCode{ 0 };
    /// 元数据
    QVariantMap m_metadata;
    /// 渲染刷新状态
    QString m_refreshState{ QStringLiteral("idle") };
    /// 当前激活工具 ID
    QString m_activeToolId;
    /// 当前输入焦点控件名称
    QString m_inputFocusWidget;
};
