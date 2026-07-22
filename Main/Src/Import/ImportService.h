#pragma once

#include <memory>
#include <functional>

#include <QObject>
#include <QString>

#include "ImportContext.h"
#include "ImportOptions.h"
#include "ImportResult.h"

class ImportDispatcher;
class UiStateCenter;
class SceneDocument2D;
class SceneTreeDockWidget;
class PropertiesPanelWidget;
class SceneEditService;

namespace Eg { class SceneManager; }

/// 导入服务：导入操作的总入口，协调格式识别、解析、文档构建和 UI 刷新
class ImportService : public QObject
{
    Q_OBJECT

public:
    explicit ImportService(QObject* parent = nullptr);
    ~ImportService() override;

    /// 设置导入分发器（注入已注册好所有读取器的分发器实例）
    void setDispatcher(ImportDispatcher* dispatcher);

    /// 设置场景管理器（用于清空场景等操作）
    void setSceneManager(Eg::SceneManager* sceneManager);

    /// 设置场景编辑服务（用于将导入的实体通过事务写入文档，支持 Undo）
    void setEditService(SceneEditService* editService);

    /// 设置状态中心（用于导入过程中的状态同步）
    void setStateCenter(UiStateCenter* stateCenter);

    /// 设置视口适配回调（导入完成后刷新视口）
    void setViewportFitCallback(std::function<void()> callback);

    /// 设置场景树刷新回调（导入完成后刷新树结构）
    void setTreeRebuildCallback(std::function<void()> callback);

    /// 设置属性面板刷新回调（导入完成后刷新属性）
    void setPropertyRefreshCallback(std::function<void()> callback);

    /// 设置工作台切换回调（导入完成后切换工作台）
    /// @param callback 参数为目标工作台 ID
    void setWorkbenchSwitchCallback(std::function<void(const QString&)> callback);

    /// 执行文件导入
    /// @param filePath 源文件路径
    /// @param options 导入选项
    /// @return 导入结果
    ImportResult importFile(const QString& filePath,
        const ImportOptions& options = ImportOptions{});

    /// 执行带完整上下文的导入
    /// @param context 导入上下文
    /// @param options 导入选项
    /// @return 导入结果
    ImportResult importWithContext(const ImportContext& context,
        const ImportOptions& options = ImportOptions{});

    /// 检查指定路径是否可导入
    bool canImport(const QString& filePath) const;

    /// 获取所有支持的导入扩展名列表
    QStringList supportedExtensions() const;

signals:
    /// 导入开始信号
    void importStarted(const QString& filePath);
    /// 导入进度信号（0.0 ~ 1.0）
    void importProgress(float progress);
    /// 导入完成信号
    void importFinished(const ImportResult& result);

private:
    /// 导入后的统一 UI 刷新流程
    void postImportRefresh(const ImportResult& result,
        const ImportOptions& options);

    /// 导入分发器（非拥有指针，由组合根管理生命周期）
    ImportDispatcher* m_dispatcher{ nullptr };
    /// 场景管理器（非拥有指针，用于 clearScene）
    Eg::SceneManager* m_sceneManager{ nullptr };
    /// 场景编辑服务（非拥有指针，用于事务化添加实体）
    SceneEditService* m_editService{ nullptr };
    /// 状态中心（非拥有指针）
    UiStateCenter* m_stateCenter{ nullptr };

    /// 视口适配回调
    std::function<void()> m_viewportFitCallback;
    /// 场景树刷新回调
    std::function<void()> m_treeRebuildCallback;
    /// 属性面板刷新回调
    std::function<void()> m_propertyRefreshCallback;
    /// 工作台切换回调
    std::function<void(const QString&)> m_workbenchSwitchCallback;
};
