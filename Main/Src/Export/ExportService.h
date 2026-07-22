#pragma once

#include <memory>
#include <functional>

#include <QObject>
#include <QString>

#include "ExportContext.h"
#include "ExportOptions.h"
#include "ExportResult.h"

class ExportDispatcher;
class UiStateCenter;

namespace Eg { class SceneManager; }

/// 导出服务：导出操作的总入口，协调数据收集、格式写入和状态回写
class ExportService : public QObject
{
    Q_OBJECT

public:
    explicit ExportService(QObject* parent = nullptr);
    ~ExportService() override;

    /// 设置导出分发器
    void setDispatcher(ExportDispatcher* dispatcher);

    /// 设置场景管理器（用于收集场景中的实体数据）
    void setSceneManager(Eg::SceneManager* sceneManager);

    /// 设置状态中心（用于导出过程中的状态同步）
    void setStateCenter(UiStateCenter* stateCenter);

    /// 执行文件导出
    /// @param filePath 目标文件路径
    /// @param options 导出选项
    /// @return 导出结果
    ExportResult exportFile(const QString& filePath,
        const ExportOptions& options = ExportOptions{});

    /// 执行带完整上下文的导出
    /// @param context 导出上下文
    /// @param options 导出选项
    /// @return 导出结果
    ExportResult exportWithContext(const ExportContext& context,
        const ExportOptions& options = ExportOptions{});

    /// 检查指定路径是否可导出
    bool canExport(const QString& filePath) const;

    /// 获取所有支持的导出扩展名列表
    QStringList supportedExtensions() const;

    /// 获取场景中所有实体（用于导出前预览或收集）
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
    void postExportRecord(const ExportResult& result,
        const ExportContext& context);

    /// 导出分发器（非拥有指针）
    ExportDispatcher* m_dispatcher{ nullptr };
    /// 场景管理器（非拥有指针）
    Eg::SceneManager* m_sceneManager{ nullptr };
    /// 状态中心（非拥有指针）
    UiStateCenter* m_stateCenter{ nullptr };
};
