/**
 * @file ExampleCustomPanel.h
 * @brief 示例：如何创建客户定制面板
 *
 * 本文件展示如何创建自定义面板并注册到 UiPanelRegistry。
 * 编译后需在应用启动时调用:
 *   UiConfigurationManager::shared().panelRegistry()->registerPanel(
 *       "ExamplePanel", [](QWidget* parent) { return new ExampleCustomPanel(parent); });
 */

#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QGroupBox>

/**
 * @brief 示例自定义面板
 *
 * 展示面板的基本结构：
 * - 标题栏（由 QDockWidget 提供）
 * - 内容区域
 * - 操作按钮
 *
 * 实际使用时，根据业务需求定制内容。
 */
class ExampleCustomPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ExampleCustomPanel(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setupUi();
    }

private:
    void setupUi()
    {
        auto* mainLayout = new QVBoxLayout(this);
        mainLayout->setContentsMargins(8, 8, 8, 8);
        mainLayout->setSpacing(8);

        // ---- 标题 ----
        mainLayout->addWidget(new QLabel(tr("Example Custom Panel")));

        // ---- 信息组 ----
        auto* infoGroup = new QGroupBox(tr("Information"));
        auto* infoLayout = new QVBoxLayout(infoGroup);
        infoLayout->addWidget(new QLabel(tr("This is a custom panel created for client customization.")));
        infoLayout->addWidget(new QLabel(tr("You can add any Qt widgets here.")));
        mainLayout->addWidget(infoGroup);

        // ---- 数据表格示例 ----
        auto* tableGroup = new QGroupBox(tr("Sample Data"));
        auto* tableLayout = new QVBoxLayout(tableGroup);
        auto* table = new QTableWidget(3, 2);
        table->setHorizontalHeaderLabels({ tr("Name"), tr("Value") });
        table->setItem(0, 0, new QTableWidgetItem(tr("Item 1")));
        table->setItem(0, 1, new QTableWidgetItem(tr("100")));
        table->setItem(1, 0, new QTableWidgetItem(tr("Item 2")));
        table->setItem(1, 1, new QTableWidgetItem(tr("200")));
        table->setItem(2, 0, new QTableWidgetItem(tr("Item 3")));
        table->setItem(2, 1, new QTableWidgetItem(tr("300")));
        tableLayout->addWidget(table);
        mainLayout->addWidget(tableGroup);

        // ---- 操作按钮 ----
        auto* buttonLayout = new QHBoxLayout();
        buttonLayout->addStretch();

        auto* refreshBtn = new QPushButton(tr("Refresh"));
        connect(refreshBtn, &QPushButton::clicked, this, &ExampleCustomPanel::onRefresh);
        buttonLayout->addWidget(refreshBtn);

        auto* configBtn = new QPushButton(tr("Configure"));
        connect(configBtn, &QPushButton::clicked, this, &ExampleCustomPanel::onConfigure);
        buttonLayout->addWidget(configBtn);

        mainLayout->addLayout(buttonLayout);

        // ---- 底部间距 ----
        mainLayout->addStretch();
    }

private slots:
    void onRefresh()
    {
        // 刷新面板数据
        qDebug() << "ExampleCustomPanel: Refresh requested";
    }

    void onConfigure()
    {
        // 打开配置对话框
        qDebug() << "ExampleCustomPanel: Configure requested";
    }

signals:
    void dataChanged();
    void configurationRequested();
};
