#pragma once

#include <QObject>
#include <QPointF>
#include <QStringList>

#include <functional>
#include <optional>

class ImportService;
class QDragEnterEvent;
class QDragMoveEvent;
class QDragLeaveEvent;
class QDropEvent;

namespace Eg
{
    class SceneManager;
}

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

    /// 注入 2D 场景管理器（非拥有指针，用于导入位图/图片文件）
    void setSceneManager(Eg::SceneManager* sceneManager);

    /// 在 QApplication 上安装应用级事件过滤器，兜底处理
    /// Windows/macOS 下 QOpenGLWidget 原生子窗口不向上冒泡拖放事件的情况
    void installAppEventFilter();

    /// 设置屏幕全局坐标 → 世界坐标转换回调（用于把导入位置放到鼠标松开处）。
    /// 入参为全局屏幕坐标（物理像素无关），返回世界坐标；无法转换时返回 std::nullopt。
    void setScreenToWorldConverter(std::function<std::optional<QPointF>(const QPoint&)> converter);

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

private:
    /// 通过 QImage 导入位图/图片文件到当前场景，图片中心放在 anchorWorld 处
    bool importImage(const QString& filePath, const QPointF& anchorWorld = QPointF(0, 0));

signals:
    /// 单个文件导入完成（导入前后各发一次，导入前 success=false）
    void sigFileImported(const QString& filePath, bool success);
    /// 一批文件拖放导入结束
    void sigDropFinished(int successCount, int failedCount);

protected:
    /// 应用级事件过滤器：拦截拖放事件（Windows/macOS 原生子窗口兜底）
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    ImportService* m_importService{ nullptr };
    Eg::SceneManager* m_sceneManager{ nullptr };
    /// 屏幕全局坐标 → 世界坐标转换器（由上层注入）
    std::function<std::optional<QPointF>(const QPoint&)> m_screenToWorld;
    /// 是否已安装应用级事件过滤器
    bool m_appFilterInstalled{ false };
};
