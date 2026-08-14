#pragma once

#include <QObject>
#include <QStringList>

class ImportService;
class QDragEnterEvent;
class QDragMoveEvent;
class QDragLeaveEvent;
class QDropEvent;

/**
 * @brief 统一文件拖放处理器 — 对接 ImportService（2D/3D 统一导入入口）
 *
 * 拖放文件进入主窗口时，按扩展名过滤受支持的文件，
 * 并通过 ImportService 调用对应格式的导入接口。
 * ImportService 会根据文件内容自动路由到 2D / 3D 场景。
 */
class FileDropHandler : public QObject
{
    Q_OBJECT

public:
    explicit FileDropHandler(QObject* parent = nullptr);

    /// 注入导入服务（非拥有指针）
    void setImportService(ImportService* service);

    // 处理 QDragEnterEvent — 仅在拖入受支持扩展名的文件时 accept
    bool handleDragEnter(QDragEnterEvent* event);
    // 处理 QDragMoveEvent
    bool handleDragMove(QDragMoveEvent* event);
    // 处理 QDragLeaveEvent
    void handleDragLeave(QDragLeaveEvent* event);
    // 处理 QDropEvent — 分派到 ImportService 导入
    bool handleDrop(QDropEvent* event);

    /// 所有受支持扩展名（小写，不含点号）
    QStringList supportedExtensions() const;

signals:
    /// 单个文件导入完成（导入前后各发一次，导入前 success=false）
    void sigFileImported(const QString& filePath, bool success);
    /// 一批文件拖放导入结束
    void sigDropFinished(int successCount, int failedCount);

private:
    ImportService* m_importService{ nullptr };
};
