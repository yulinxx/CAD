#include "UiPropertiesPanel.h"

#include <functional>
#include <memory>

#include <QAbstractItemModel>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QMessageBox>
#include <QModelIndex>
#include <QPushButton>
#include <QSpinBox>
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

    // 点列表编辑器：索引选择 + 坐标
    // 索引使用 QSpinBox（而非把 N 个索引全部塞进 QComboBox）：
    //   - O(1) 内存，顶点再多也不物化索引
    //   - 支持键盘直接跳转到任意索引（如输入 5000）
    // 坐标本身不预取，通过 pointProvider 按需加载（懒加载）
    class PointListEditor : public QWidget
    {
    public:
        explicit PointListEditor(QWidget* parent = nullptr)
            : QWidget(parent)
        {
            auto* layout = new QVBoxLayout(this);
            layout->setContentsMargins(2, 2, 2, 2);
            layout->setSpacing(4);
            auto* indexRow = new QHBoxLayout();
            indexRow->addWidget(new QLabel(QStringLiteral("Index:"), this));
            m_index = new QSpinBox(this);
            m_index->setRange(0, 0);
            m_index->setMinimumWidth(90);
            indexRow->addWidget(m_index, 1);
            m_countLabel = new QLabel(this);
            indexRow->addWidget(m_countLabel);
            layout->addLayout(indexRow);
            m_coord = new PointCoordEditor(this);
            layout->addWidget(m_coord);

            // 切换索引时，通过坐标提供器按需加载该索引对应的坐标
            connect(m_index, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int idx) {
                if (m_provider)
                {
                    m_coord->setPoint(m_provider(idx));
                }
            });
        }

        /// 设置"按索引取坐标"的提供器（由代理注入，按需读取，懒加载）
        void setPointProvider(std::function<QPointF(int)> provider)
        {
            m_provider = std::move(provider);
        }

        /// 配置索引范围（count 可为任意大，不物化）
        void setup(int count, int current, const QString& label)
        {
            Q_UNUSED(label);
            const int last = qMax(0, count - 1);
            m_index->blockSignals(true);
            m_index->setRange(0, last);
            m_index->setValue(qBound(0, current, last));
            m_index->blockSignals(false);
            m_countLabel->setText(QStringLiteral("/ %1").arg(count));
        }

        int index() const
        {
            return m_index->value();
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
        QSpinBox* m_index{ nullptr };
        QLabel* m_countLabel{ nullptr };
        PointCoordEditor* m_coord{ nullptr };
        std::function<QPointF(int)> m_provider;
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

    // 是否属于"复合编辑"类型：需要多控件/较多空间，改用弹窗编辑，避免内联编辑器被压缩到行高内不可见
    bool isComplexEditType(PropertyEditType type)
    {
        return type == PropertyEditType::Point2d || type == PropertyEditType::PointList
            || type == PropertyEditType::Color;
    }

    // 复合属性（点/点列表/颜色）编辑弹窗：提供充足空间，保证控件完整显示
    class PropertyEditDialog : public QDialog
    {
    public:
        PropertyEditDialog(const PropertyItem& item, std::shared_ptr<IPropertyEditTarget> target, QWidget* parent = nullptr)
            : QDialog(parent)
            , m_item(item)
            , m_target(std::move(target))
        {
            setWindowTitle(tr("Edit %1").arg(m_item.name));
            setMinimumWidth(360);

            auto* layout = new QVBoxLayout(this);
            layout->setContentsMargins(12, 12, 12, 12);
            layout->setSpacing(12);

            auto* form = new QFormLayout();
            form->setLabelAlignment(Qt::AlignRight);

            switch (m_item.editType)
            {
            case PropertyEditType::Point2d:
            {
                m_point = new PointCoordEditor(this);
                m_point->setPoint(m_target ? m_target->pointAt(m_item, 0) : QPointF());
                form->addRow(tr("Coordinates:"), m_point);
                break;
            }
            case PropertyEditType::PointList:
            {
                m_list = new PointListEditor(this);
                const QVariantMap data = m_item.editData.toMap();
                const int count = data.value(QStringLiteral("count"), 0).toInt();
                const int current = data.value(QStringLiteral("currentIndex"), 0).toInt();
                const QString label = data.value(QStringLiteral("label"), tr("Point")).toString();
                m_list->setup(count, current, label);
                m_list->setPointProvider(
                    [target = this->m_target, item = this->m_item](int idx) -> QPointF {
                        return target ? target->pointAt(item, idx) : QPointF();
                    });
                m_list->setPoint(m_target ? m_target->pointAt(m_item, current) : QPointF());
                form->addRow(tr("Vertices:"), m_list);
                break;
            }
            case PropertyEditType::Color:
            {
                m_color = new ColorEditor(this);
                QColor c(m_item.value);
                m_color->setColor(c.isValid() ? c : QColor(Qt::white));
                form->addRow(tr("Color:"), m_color);
                break;
            }
            default:
                break;
            }
            layout->addLayout(form);

            auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
            connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
                if (commit())
                {
                    accept();
                }
                else
                {
                    QMessageBox::warning(this, tr("Edit"), tr("Invalid value, change was not applied."));
                }
            });
            connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
            layout->addWidget(buttons);
        }

        /// 应用修改（成功返回 true）
        bool commit()
        {
            if (!m_target)
            {
                return false;
            }
            if (m_point)
            {
                return m_target->editPointAt(m_item, 0, m_point->point());
            }
            if (m_list)
            {
                return m_target->editPointAt(m_item, m_list->index(), m_list->point());
            }
            if (m_color)
            {
                return m_target->editValue(m_item, m_color->color().name(QColor::HexArgb));
            }
            return false;
        }

    private:
        PropertyItem m_item;
        std::shared_ptr<IPropertyEditTarget> m_target;
        PointCoordEditor* m_point{ nullptr };
        PointListEditor* m_list{ nullptr };
        ColorEditor* m_color{ nullptr };
    };
}  // namespace

// ---- 值列的内联编辑代理 ----
// 属性名列（第 0 列）只读，禁止原地编辑字段名
class ReadOnlyFieldDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QWidget* createEditor(QWidget*, const QStyleOptionViewItem&, const QModelIndex&) const override
    {
        return nullptr;  // 取消编辑
    }
};

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
        case PropertyEditType::Point2d:
        case PropertyEditType::PointList:
        case PropertyEditType::Color:
        default:
            // 复合类型改由 PropertyEditDialog 弹窗编辑（createEditor 返回 nullptr 表示不进入内联编辑）
            return nullptr;
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
    // 行高适当放大，保证内联编辑框/下拉框完整显示
    m_tree->setStyleSheet(QStringLiteral("QTreeWidget::item { min-height: 26px; }"));
    // 编辑统一在 itemDoubleClicked 中手动处理：标量内联、复合类型弹窗
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    layout->addWidget(m_tree);

    // 值列使用内联编辑代理；未设置编辑目标时不会进入编辑态
    m_delegate = new PropertyItemDelegate(this);
    m_delegate->setCommitCallback([this]() {
        emit sigPropertyEdited();
    });
    m_tree->setItemDelegateForColumn(1, m_delegate);
    // 属性名列只读，避免误改字段名
    m_tree->setItemDelegateForColumn(0, new ReadOnlyFieldDelegate(this));

    // 双击编辑：标量内联编辑，复合属性（点/点列表/颜色）弹窗编辑，保证控件完整可见
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int column) {
        if (column != 1 || !item)
        {
            return;
        }
        const PropertyItem pi = item->data(1, PropertyItemRole).value<PropertyItem>();
        if (!pi.editable || !m_editTarget)
        {
            return;
        }

        if (isComplexEditType(pi.editType))
        {
            PropertyEditDialog dlg(pi, m_editTarget, this);
            if (dlg.exec() == QDialog::Accepted)
            {
                emit sigPropertyEdited();
            }
        }
        else
        {
            m_tree->editItem(item, 1);
        }
    });
}

void PropertiesPanelWidget::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::LanguageChange && m_tree)
    {
        m_tree->setHeaderLabels({ tr("Field"), tr("Value") });
    }
    QWidget::changeEvent(event);
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
        // 保存 PropertyItem 供内联编辑器读取（两列都保存，代理按当前编辑列读取）
        child->setData(0, PropertyItemRole, QVariant::fromValue(item));
        child->setData(1, PropertyItemRole, QVariant::fromValue(item));

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
