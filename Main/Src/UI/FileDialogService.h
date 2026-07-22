#pragma once

#include <QString>

class QWidget;
namespace Fio { enum class FileFormat; }

/**
 * @class FileDialogService
 * @brief 文件对话框服务 —— 封装所有 QFileDialog 调用与格式过滤器映射
 *
 * 将文件打开/保存/导入/导出的对话框逻辑从 ApplicationCompositionRoot 剥离，
 * 组合根只负责业务编排，不直接触碰 Qt 文件对话框 API。
 */
class FileDialogService
{
public:
    /// 获取打开文件路径（弹出对话框）
    /// @param parent 父窗口
    /// @param title 对话框标题
    /// @param filter 文件过滤器
    /// @return 用户选择的文件路径，取消则返回空
    static QString getOpenFileName(QWidget* parent, const QString& title, const QString& filter);

    /// 获取保存文件路径（弹出对话框）
    /// @param parent 父窗口
    /// @param title 对话框标题
    /// @param filter 文件过滤器
    /// @return 用户选择的文件路径，取消则返回空
    static QString getSaveFileName(QWidget* parent, const QString& title, const QString& filter);

    // ---- 过滤器映射 ----

    /// 打开/导入时的通用过滤器（所有支持格式）
    static QString allSupportedFilter();

    /// 按导入格式返回对应过滤器
    static QString importFilterForFormat(Fio::FileFormat fmt);

    /// 按导出格式返回对应过滤器
    static QString exportFilterForFormat(Fio::FileFormat fmt);

    /// 打开文件对话框的默认过滤器
    static QString openFileFilter();

    /// 保存文件对话框的默认过滤器
    static QString saveFileFilter();

    /// 图片导入过滤器
    static QString imageImportFilter();
};
