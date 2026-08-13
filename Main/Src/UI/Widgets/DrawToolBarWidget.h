#pragma once

#include <QWidget>
#include <QMap>
#include <QString>
#include <QVector>

class QToolButton;
class OperationBus;

/**
 * @brief 绘图工具按钮的描述信息
 *
 * 由 CommandCatalog 等外部命令定义源注入，
 * 确保工具栏和菜单使用同一套命令 ID。
 */
struct DrawToolEntry
{
    QString commandId;     // 命令 ID，与 CommandCatalog 的 toolName 对齐
    QString displayName;   // 按钮显示文本
    QString tooltip;       // 详细提示文本
    QString shortcut;      // 快捷键提示（可选）
    QString iconResource;  // SVG 图标资源路径（如 ":/ui/common/Icons/Tools/line.svg"），为空时回退为文字按钮
};

class DrawToolBarWidget : public QWidget
{
    Q_OBJECT

public:
    explicit DrawToolBarWidget(QWidget* parent = nullptr);
    ~DrawToolBarWidget() override;

public:
    void setOperationBus(OperationBus* bus);

    /**
     * @brief 注入工具定义列表，替代硬编码的工具 ID
     *
     * 应在 setOperationBus 之前调用，
     * 确保工具栏按钮使用与 CommandCatalog 一致的命令 ID。
     * @param tools 工具条目列表
     */
    void setToolDefinitions(const QVector<DrawToolEntry>& tools);

    void updateActiveTool(const QString& toolId);

    QString currentActiveTool() const;

private slots:
    void onToolButtonClicked();

private:
    void createToolButtons();
    void setButtonChecked(const QString& toolId, bool checked);

    QMap<QString, QToolButton*> m_toolButtons;
    QVector<DrawToolEntry> m_toolDefinitions;
    QString m_activeToolId;
    OperationBus* m_operationBus{ nullptr };
};
