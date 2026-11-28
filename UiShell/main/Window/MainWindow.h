#pragma once

#include <QMainWindow>
#include <QString>

class QCloseEvent;

// 主窗口
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(const QString& settingsGroup, QWidget* parent = nullptr);
    ~MainWindow() override;

public:
    // 恢复上次保存的窗口状态
    bool restoreWindowState();

    // 保存窗口状态
    void saveWindowState() const;

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    QString m_settingsGroup;

    // 只有启动成功并调用过 restoreWindowState() 才保存：启动失败的窗口不会覆盖上次的状态。
    bool m_persistState = false;
};
