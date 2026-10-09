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
    /// 当前任务进度 (0-100)，-1 表示无进行中的任务
    int progress{ -1 };
    /// 当前状态提示（用于命令引导、导入导出提示等展示文本）
    QString statusPrompt;
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



    /// 获取当前图层 ID
    QString currentLayerId() const;














    /// 是否有未保存更改
    bool dirty() const;

    /// 获取当前任务进度 (-1 表示无任务)
    int progress() const;








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


    /// 计数式繁忙：pushBusy/popBusy 配对使用，仅 0↔1 时触发信号
    void pushBusy();
    void popBusy();

    /// 设置脏状态
    /// @param dirty 是否有未保存更改
    void setDirty(bool dirty);




    /// 设置元数据（合并语义：单键更新，保留其它键）
    /// @param key 键
    /// @param value 值
    void updateMetadata(const QString& key, const QVariant& value);

    /// 批量更新元数据（合并语义）
    /// @param metadata 要合并的键值对
    void updateMetadata(const QVariantMap& metadata);

    /// 设置元数据（整包替换语义，**慎用：会清空其它键**）
    /// @param metadata 元数据映射
    void setMetadata(const QVariantMap& metadata);

    /// 设置当前状态提示
    /// @param prompt 状态提示内容
    void setStatusPrompt(const QString& prompt);


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









    /// 繁忙状态变更信号
    void busyChanged(bool busy);

    /// 脏状态变更信号
    void dirtyChanged(bool dirty);



    /// 元数据变更信号
    void metadataChanged();



private:
    /// ===== 分组状态（P1-5）=====
    /// 新增状态必须归入以下某一组；禁止新增顶层字段。
    /// 需要新的状态类别时，应另立中心或经架构评审后扩展本文件（见《框架冗余与复杂度审查》§3.3.1）。

    /// 工作台域：切换工作台时随快照保存/恢复的标识类状态
    struct WorkspaceState
    {
        QString workbenchId{ QStringLiteral("default") };
        QString themeId{ QStringLiteral("system") };
        QString viewMode{ QStringLiteral("none") };
        QString documentId{ QStringLiteral("none") };
        QString activeToolId;
        QString inputFocusWidget;
    };

    /// 命令域：命令生命周期、启用反馈与任务/错误状态
    struct CommandState
    {
        QString commandId{ QStringLiteral("idle") };
        QString commandPhase{ QStringLiteral("idle") };
        QString commandOwner{ QStringLiteral("none") };
        QString commandType{ QStringLiteral("none") };
        int busyCount{ 0 };  ///< 计数式繁忙：0=空闲，>0=繁忙
        bool dirty{ false };
        int progress{ -1 };  ///< (0-100)，-1 表示无进行中的任务
        QString statusPrompt;
    };

    /// 视图域：图层、渲染刷新、元数据与交互指针
    struct ViewState
    {
        QString layerId{ QStringLiteral("default") };
        QString refreshState{ QStringLiteral("idle") };
        QVariantMap metadata;
    };

    /// 选择域：当前选择的文本化描述
    struct SelectionState
    {
        QString text;
        QString source{ QStringLiteral("none") };
        QString type{ QStringLiteral("none") };
    };

    WorkspaceState m_workspace;
    CommandState m_command;
    ViewState m_view;
    SelectionState m_selection;
};
