#include "UiPropertiesPanel.h"

#include <functional>
#include <memory>

#include <QAbstractItemModel>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QModelIndex>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include "UI/Dlg/IPropertyEditTarget.h"

namespace
{
    // 树节点中保存的 PropertyItem 角色
    constexpr int PropertyItemRole = Qt::UserRole + 1;

    PropertyItem itemFromIndex(const QModelIndex& index)
    {
        QVariant v = index.data(PropertyItemRole);
        return v.canConvert<PropertyItem>() ? v.value<PropertyItem>() : PropertyItem();
    }

    QDoubleSpinBox* makeCoordSpin(QWidget* parent)
    {
        auto* spin = new QDoubleSpinBox(parent);
        spin->setRange(-1e9, 1e9);
        spin->setDecimals(4);
        return spin;
    }

    // ---- 编辑用编辑器组件 ----

    // 点（Point2d / PointList）坐标编辑器：X/Y 两个数值框
    class PointCoordEditor : public QWidget
    {
    public:
        explicit PointCoordEditor(QWidget* parent = nullptr)
            : QWidget(parent)
        {
            auto* layout = new QHBoxLayout(this);
            layout->setContentsMargins(2, 2, 2, 2);
            layout->setSpacing(4);
            m_x = makeCoordSpin(this);
            m_y = makeCoordSpin(this);
            layout->addWidget(new QLabel(QStringLiteral("X:"), this));
            layout->addWidget(m_x, 1);
            layout->addWidget(new QLabel(QStringLiteral("Y:"), this));
            layout->addWidget(m_y, 1);
        }

        void setPoint(const QPointF& p)
        {
            m_x->setValue(p.x());
            m_y->setValue(p.y());
        }

        QPointF point() const
        {
            return QPointF(m_x->value(), m_y->value());
        }

    private:
        QDoubleSpinBox* m_x{ nullptr };
        QDoubleSpinBox* m_y{ nullptr };
    };

    // 点列表编辑器：索引下拉 + 坐标
    class PointListEditor : public QWidget
    {
    public:
        explicit PointListEditor(QWidget* parent = nullptr)
            : QWidget(parent)
        {
            auto* layout = new QVBoxLayout(this);
            layout->setContentsMargins(2, 2, 2, 2);
            layout->setSpacing(2);
            auto* indexRow = new QHBoxLayout();
            indexRow->addWidget(new QLabel(QStringLiteral("Index:"), this));
            m_index = new QComboBox(this);
            indexRow->addWidget(m_index, 1);
            layout->addLayout(indexRow);
            m_coord = new PointCoordEditor(this);
            layout->addWidget(m_coord);
        }

        void setup(int count, int current, const QString& label)
        {
            m_index->blockSignals(true);
            m_index->clear();
            for (int i = 0; i < count; ++i)
            {
                m_index->addItem(QStringLiteral("%1 %2").arg(label).arg(i));
            }
            m_index->setCurrentIndex(qBound(0, current, count - 1));
            m_index->blockSignals(false);
        }

        int index() const
        {
            return m_index->currentIndex();
        }

        void setPoint(const QPointF& p)
        {
            m_coord->setPoint(p);
        }

        QPointF point() const
        {
            return m_coord->point();
        }

    private:
        QComboBox* m_index{ nullptr };
        PointCoordEditor* m_coord{ nullptr };
    };

    // 颜色编辑器：按钮弹出选色对话框
    class ColorEditor : public QWidget
    {
    public:
        explicit ColorEditor(QWidget* parent = nullptr)
            : QWidget(parent)
        {
            auto* layout = new QHBoxLayout(this);
            layout->setContentsMargins(2, 2, 2, 2);
            m_button = new QPushButton(this);
            m_button->setMinimumHeight(24);
            layout->addWidget(m_button);
            connect(m_button, &QPushButton::clicked, this, &ColorEditor::chooseColor);
        }

        void setColor(const QColor& c)
        {
            m_color = c;
            m_button->setStyleSheet(
                QStringLiteral("background-color: %1; color: %2;")
                    .arg(c.name(QColor::HexRgb))
                    .arg(c.lightnessF() > 0.5 ? QStringLiteral("black") : QStringLiteral("white")));
            m_button->setText(c.name(QColor::HexArgb).toUpper());
        }

        QColor color() const
        {
            return m_color;
        }

    private:
        void chooseColor()
        {
            QColor c = QColorDialog::getColor(m_color, this);
            if (c.isValid())
            {
                setColor(c);
            }
        }

        QPushButton* m_button{ nullptr };
        QColor m_color{ Qt::white };
    };
}  // namespace

// ---- 值列的内联编辑代理 ----
class PropertyItemDelegate : public QStyledItemDelegate
{
public:
    explicit PropertyItemDelegate(QObject* parent = nullptr)
        : QStyledItemDelegate(parent)
    {
    }

    /// 绑定当前编辑目标与提交回调（编辑成功时触发模型重建）
    void setEditTarget(std::shared_ptr<IPropertyEditTarget> target)
    {
        m_target = std::move(target);
    }

    void setCommitCallback(std::function<void()> cb)
    {
        m_onCommitted = std::move(cb);
    }

    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem&, const QModelIndex& index) const override
    {
        const PropertyItem item = itemFromIndex(index);
        switch (item.editType)
        {
        case PropertyEditType::Number:
        case PropertyEditType::Integer:
        case PropertyEditType::Text:
        {
            auto* edit = new QLineEdit(parent);
            return edit;
        }
        case PropertyEditType::Point2d:
            return new PointCoordEditor(parent);
        case PropertyEditType::PointList:
            return new PointListEditor(parent);
        case PropertyEditType::ComboBox:
        {
            auto* combo = new QComboBox(parent);
            combo->addItems(item.editData.toStringList());
            return combo;
        }
        case PropertyEditType::CheckBox:
        {
            auto* combo = new QComboBox(parent);
            combo->addItems({ tr("No"), tr("Yes") });
            return combo;
        }
        case PropertyEditType::Color:
            return new ColorEditor(parent);
        default:
            return new QLineEdit(parent);
        }
    }

    void setEditorData(QWidget* editor, const QModelIndex& index) const override
    {
        const PropertyItem item = itemFromIndex(index);

        if (auto* line = dynamic_cast<QLineEdit*>(editor))
        {
            line->setText(item.value);
            return;
        }

        if (auto* combo = dynamic_cast<QComboBox*>(editor))
        {
            int idx = combo->findText(item.value);
            combo->setCurrentIndex(qMax(0, idx));
            return;
        }

        if (auto* pt = dynamic_cast<PointCoordEditor*>(editor))
        {
            QPointF p = m_target ? m_target->pointAt(item, 0) : QPointF();
            pt->setPoint(p);
            return;
        }

        if (auto* pl = dynamic_cast<PointListEditor*>(editor))
        {
            const QVariantMap data = item.editData.toMap();
            const int count = data.value(QStringLiteral("count"), 0).toInt();
            const int current = data.value(QStringLiteral("currentIndex"), 0).toInt();
            const QString label = data.value(QStringLiteral("label"), tr("Point")).toString();
            pl->setup(count, current, label);
            QPointF p = m_target ? m_target->pointAt(item, current) : QPointF();
            pl->setPoint(p);
            return;
        }

        if (auto* ce = dynamic_cast<ColorEditor*>(editor))
        {
            QColor c(item.value);
            ce->setColor(c.isValid() ? c : QColor(Qt::white));
            return;
        }
    }

    void setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const override
    {
        if (!m_target)
        {
            return;
        }

        const PropertyItem item = itemFromIndex(index);
        bool applied = false;
        QString displayValue;

        if (auto* line = dynamic_cast<QLineEdit*>(editor))
        {
            switch (item.editType)
            {
            case PropertyEditType::Number:
            {
                bool ok = false;
                const double v = line->text().toDouble(&ok);
                if (ok)
                {
                    applied = m_target->editValue(item, v);
                    if (applied)
                    {
                        displayValue = line->text();
                    }
                }
                break;
            }
            case PropertyEditType::Integer:
            {
                bool ok = false;
                const int v = line->text().toInt(&ok);
                if (ok)
                {
                    applied = m_target->editValue(item, v);
                    if (applied)
                    {
                        displayValue = line->text();
                    }
                }
                break;
            }
            default:
                applied = m_target->editValue(item, line->text());
                if (applied)
                {
                    displayValue = line->text();
                }
                break;
            }
        }
        else if (auto* combo = dynamic_cast<QComboBox*>(editor))
        {
            if (item.editType == PropertyEditType::CheckBox)
            {
                applied = m_target->editValue(item, combo->currentIndex() == 1);
            }
            else
            {
                applied = m_target->editValue(item, combo->currentText());
                displayValue = combo->currentText();
            }
        }
        else if (auto* pt = dynamic_cast<PointCoordEditor*>(editor))
        {
            applied = m_target->editPointAt(item, 0, pt->point());
            if (applied)
            {
                displayValue = QStringLiteral("(%1, %2)").arg(pt->point().x(), 0, 'f', 3).arg(pt->point().y(), 0, 'f', 3);
            }
        }
        else if (auto* pl = dynamic_cast<PointListEditor*>(editor))
        {
            applied = m_target->editPointAt(item, pl->index(), pl->point());
        }
        else if (auto* ce = dynamic_cast<ColorEditor*>(editor))
        {
            applied = m_target->editValue(item, ce->color().name(QColor::HexArgb));
            if (applied)
            {
                displayValue = ce->color().name(QColor::HexArgb);
            }
        }

        // 写回展示值（近似）；随后由绑定层根据提交回调重建模型，保证派生字段刷新
        if (applied)
        {
            if (!displayValue.isEmpty())
            {
                model->setData(index, displayValue, Qt::DisplayRole);
            }
            if (m_onCommitted)
            {
                m_onCommitted();
            }
        }
    }

private:
    std::shared_ptr<IPropertyEditTarget> m_target;
    std::function<void()> m_onCommitted;
};

PropertiesPanelWidget::PropertiesPanelWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabels({ tr("Field"), tr("Value") });
    m_tree->setColumnCount(2);
    m_tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tree->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    layout->addWidget(m_tree);

    // 值列使用内联编辑代理；未设置编辑目标时不会进入编辑态
    m_delegate = new PropertyItemDelegate(this);
    m_delegate->setCommitCallback([this]() {
        emit sigPropertyEdited();
    });
    m_tree->setItemDelegateForColumn(1, m_delegate);
}

void PropertiesPanelWidget::setEditTarget(std::shared_ptr<IPropertyEditTarget> target)
{
    m_editTarget = std::move(target);
    m_delegate->setEditTarget(m_editTarget);
    refresh();
}

void PropertiesPanelWidget::setPropertiesData(const PropertiesData& data)
{
    m_data = data;
    refresh();
}

void PropertiesPanelWidget::setWorkbenchMode(WorkbenchMode mode)
{
    m_data.mode = mode;
    refresh();
}

void PropertiesPanelWidget::setStateText(const QString& text)
{
    m_data.stateText = text;
    refresh();
}

void PropertiesPanelWidget::setSelectionText(const QString& text)
{
    m_data.selectionText = text;
    refresh();
}

void PropertiesPanelWidget::setObjectDetails(const QString& title, const QStringList& lines)
{
    m_data.objectTitle = title;
    m_data.objectLines = lines;
    refresh();
}

void PropertiesPanelWidget::setPropertyModel(const PropertyModel& model)
{
    m_model = model;
    m_hasModel = true;
    refresh();
}

void PropertiesPanelWidget::refresh()
{
    if (m_tree)
    {
        m_tree->clear();
    }
    syncText();
}

void PropertiesPanelWidget::syncText()
{
    if (!m_tree)
    {
        return;
    }

    // 优先渲染算法层生成的属性模型；有选中对象时展示模型，否则回退到通用信息文本
    if (m_hasModel)
    {
        renderPropertyModel();
        return;
    }

    renderInfoText();
}

void PropertiesPanelWidget::renderPropertyModel()
{
    if (!m_model.hasSelection)
    {
        // 无选中对象：展示空态
        new QTreeWidgetItem(m_tree, { tr("Selection"), tr("No selection") });
        return;
    }

    if (!m_model.typeName.isEmpty())
    {
        new QTreeWidgetItem(m_tree, { tr("Object"), m_model.typeName });
    }

    const bool editable = static_cast<bool>(m_editTarget);

    // 按 category 分组展示；category 首次出现顺序即为分组顺序
    QMap<QString, QTreeWidgetItem*> groups;
    for (const PropertyItem& item : m_model.items)
    {
        if (item.name.isEmpty())
        {
            continue;
        }

        QTreeWidgetItem* parent = groups.value(item.category, nullptr);
        if (!parent)
        {
            parent = new QTreeWidgetItem(m_tree);
            parent->setText(0, item.category.isEmpty() ? tr("General") : item.category);
            parent->setFlags(parent->flags() & ~Qt::ItemIsEditable);
            groups.insert(item.category, parent);
        }

        auto* child = new QTreeWidgetItem(parent, { item.name, item.value });
        if (!item.tooltip.isEmpty())
        {
            child->setToolTip(0, item.tooltip);
            child->setToolTip(1, item.tooltip);
        }
        // 保存 PropertyItem 供内联编辑器读取
        child->setData(0, PropertyItemRole, QVariant::fromValue(item));

        // 仅可编辑项且存在编辑目标时允许双击编辑
        if (item.editable && editable)
        {
            child->setFlags(child->flags() | Qt::ItemIsEditable);
        }
        else
        {
            child->setFlags(child->flags() & ~Qt::ItemIsEditable);
        }
    }

    m_tree->expandAll();
}

void PropertiesPanelWidget::renderInfoText()
{
    new QTreeWidgetItem(m_tree, { tr("State"), m_data.stateText });
    new QTreeWidgetItem(m_tree, { tr("Selection"), m_data.selectionText });
    new QTreeWidgetItem(m_tree, { tr("Object"), m_data.objectTitle });

    if (!m_data.documentType.isEmpty())
    {
        new QTreeWidgetItem(m_tree, { tr("Document"), m_data.documentType });
    }
    if (!m_data.documentStatus.isEmpty())
    {
        new QTreeWidgetItem(m_tree, { tr("Status"), m_data.documentStatus });
    }

    for (const QString& field : m_data.modeSpecificFields)
    {
        const int colonIndex = field.indexOf(QStringLiteral(":"));
        if (colonIndex > 0)
        {
            new QTreeWidgetItem(m_tree, { field.left(colonIndex).trimmed(), field.mid(colonIndex + 1).trimmed() });
        }
        else
        {
            new QTreeWidgetItem(m_tree, { tr("Detail"), field });
        }
    }

    for (const QString& line : m_data.objectLines)
    {
        new QTreeWidgetItem(m_tree, { tr("Detail"), line });
    }
}
