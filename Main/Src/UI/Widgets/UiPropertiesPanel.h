#pragma once

#include <memory>

#include <QString>
#include <QStringList>
#include <QWidget>

#include "UI/Dlg/PropertyModel.h"

class QTreeWidget;
class QTreeWidgetItem;
class IPropertyEditTarget;

class PropertiesPanelWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit PropertiesPanelWidget(QWidget* parent = nullptr);

public:
    enum class WorkbenchMode
    {
        Unknown,
        TwoD,
        ThreeD
    };

    struct PropertiesData
    {
        QString objectTitle;
        QStringList objectLines;
        WorkbenchMode mode{ WorkbenchMode::Unknown };
        QString documentType;
        QString documentStatus;
        QStringList modeSpecificFields;
    };

public:
    void setPropertiesData(const PropertiesData& data);
    void setWorkbenchMode(WorkbenchMode mode);
    void setObjectDetails(const QString& title, const QStringList& lines);
    void refresh();

    /// 设置属性模型（算法层产物）。UI 面板仅作为该模型的消费者，
    /// 与算法/数据层完全解耦；面板不存在或被替换都不影响数据产生端。
    void setPropertyModel(const PropertyModel& model);

    /// 设置编辑目标（算法层实现，负责应用修改并集成撤销）。
    /// 面板仅通过 IPropertyEditTarget 接口提交编辑，不感知引擎/撤销细节。
    void setEditTarget(std::shared_ptr<IPropertyEditTarget> target);

    /// 设置选中项锁定态（来自命令中枢的选择上下文快照）。锁定态下禁用双击编辑，
    /// 使属性面板与工具栏/菜单/右键菜单的锁定规则实时一致（单一事件总线驱动）。
    void setLockState(bool locked);

signals:
    /// 某属性被成功编辑（已入撤销栈）。由绑定层监听并触发模型重建。
    void sigPropertyEdited();

protected:
    void changeEvent(QEvent* event) override;

private:
    void syncText();
    void renderPropertyModel();
    void renderInfoText();

private:
    QTreeWidget* m_tree{ nullptr };
    PropertiesData m_data;
    PropertyModel m_model;
    bool m_hasModel{ false };
    std::shared_ptr<IPropertyEditTarget> m_editTarget;
    class PropertyItemDelegate* m_delegate{ nullptr };
    /// 选中项是否处于锁定（图层锁或实体锁），禁用双击内联/弹窗编辑
    bool m_locked{ false };
};
