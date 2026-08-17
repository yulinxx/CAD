#pragma once

#include <memory>
#include <functional>

#include <QObject>
#include <QString>

#include "ExportContext.h"
#include "ExportOptions.h"
#include "ExportResult.h"
#include "FileIO/IFileParser.h"

class ExportDispatcher;

namespace Eg
{
    class SceneManager;
    class SceneManager3D;
}

/// 导出服务：导出操作的总入口，协调数据收集、格式写入和状态回写
class ExportService : public QObject
{
    Q_OBJECT

public:
    explicit ExportService(QObject* parent = nullptr);
    ~ExportService() override;

    /// 设置导出分发器
    void setDispatcher(ExportDispatcher* dispatcher);

    /// 设置场景管理器（用于收集 2D 图元数据）
    void setSceneManager(Eg::SceneManager* sceneManager);

    /// 设置 3D 场景管理器（用于收集 3D 网格图元）
    void setSceneManager3D(Eg::SceneManager3D* sceneManager3D);

    /// 设置忙状态回调（替代旧的 UiStateCenter 直接依赖）
    void setBusyStateCallback(std::function<void(bool)> callback);
    /// 设置状态栏提示回调（替代旧的 UiStateCenter 直接依赖）
    void setStatusPromptCallback(std::function<void(const QString&)> callback);

    /// 执行文件导出
    /// @param filePath 目标文件路径
    /// @param options 导出选项
    /// @return 导出结果
    ExportResult exportFile(const QString& filePath, const ExportOptions& options = ExportOptions{});

    /// 执行带完整上下文的导出
    /// @param context 导出上下文
    /// @param options 导出选项
    /// @return 导出结果
    ExportResult exportWithContext(const ExportContext& context, const ExportOptions& options = ExportOptions{});

    /// 检查指定路径是否可导出
    bool canExport(const QString& filePath) const;

    /// 获取所有支持的导出扩展名列表
    QStringList supportedExtensions() const;

    /// 获取场景中所有图元（用于导出前预览或收集）
    Fio::VecSyEntityPtr collectAllEntities() const;

signals:
    /// 导出开始信号
    void exportStarted(const QString& filePath);
    /// 导出进度信号（0.0 ~ 1.0）
    void exportProgress(float progress);
    /// 导出完成信号
    void exportFinished(const ExportResult& result);

private:
    /// 导出后的状态回写
    void postExportRecord(const ExportResult& result, const ExportContext& context);

    /// 导出分发器（非拥有指针）
    ExportDispatcher* m_dispatcher{ nullptr };
    /// 2D 场景管理器（非拥有指针）
    Eg::SceneManager* m_sceneManager{ nullptr };
    /// 3D 场景管理器（非拥有指针）
    Eg::SceneManager3D* m_sceneManager3D{ nullptr };
    /// 忙状态回调（替代 UiStateCenter 直接调用）
    std::function<void(bool)> m_busyStateCallback;
    /// 状态栏提示回调（替代 UiStateCenter 直接调用）
    std::function<void(const QString&)> m_statusPromptCallback;
};
