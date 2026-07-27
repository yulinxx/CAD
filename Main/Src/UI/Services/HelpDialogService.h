#pragma once

#include <QString>

class QWidget;

/**
 * @class HelpDialogService
 * @brief 帮助弹窗服务 —— 封装 About / Settings / Documentation / Shortcuts 弹窗
 *
 * 将帮助相关的 QMessageBox 弹窗逻辑从 ApplicationCompositionRoot 剥离。
 * 所有弹窗的内容、布局、图标设置集中管理，方便后续演进为独立对话框类。
 */
class HelpDialogService
{
public:
    /// 显示关于对话框
    static void showAboutDialog(QWidget* parent);

    /// 显示设置提示对话框
    static void showSettingsDialog(QWidget* parent);

    /// 显示文档提示对话框
    static void showDocumentationDialog(QWidget* parent);

    /// 显示键盘快捷键对话框
    static void showShortcutsDialog(QWidget* parent);

    // ---- 通用错误/信息弹窗（供文件操作使用）----

    /// 显示警告消息框
    static void showWarning(QWidget* parent, const QString& title, const QString& message);

    /// 显示信息消息框
    static void showInformation(QWidget* parent, const QString& title, const QString& message);

    /// 显示问题确认对话框，返回用户选择
    /// @return QMessageBox::Yes / QMessageBox::No / QMessageBox::Cancel
    static int showQuestion(QWidget* parent, const QString& title, const QString& message);

    /// 弹出双精度数值输入对话框
    /// @param parent 父窗口
    /// @param title 对话框标题
    /// @param label 提示文字
    /// @param value 默认值
    /// @param min 最小值
    /// @param max 最大值
    /// @param decimals 小数位数
    /// @param ok 输出：用户是否点击了确定
    /// @return 用户输入的数值
    static double getDouble(QWidget* parent, const QString& title, const QString& label,
        double value, double min, double max, int decimals, bool* ok);
};
