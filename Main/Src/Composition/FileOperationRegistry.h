#pragma once

#include "FileIO/FileFormat.h"
#include "FileIO/FileIOManager.h"

#include <QString>
#include <memory>
#include <string>
#include <functional>

class OperationBus;
class UiStateCenter;
class LayerPersistenceBridge;
class PersistenceService;
class ImportService;
class ExportService;
class RecentFileService;
class HelpDialogService;

namespace Eg
{
    class SceneManager;
}

class QWidget;

/**
 * @brief 文件操作注册器的依赖配置
 *
 * P3b 参数收敛：将构造函数 11 个裸参数封装为结构体，
 * 降低调用方记忆负担，便于后续扩展。
 */
struct FileOperationConfig
{
    OperationBus* bus = nullptr;
    Eg::SceneManager* sceneManager = nullptr;
    ImportService* importService = nullptr;
    ExportService* exportService = nullptr;
    RecentFileService* recentFiles = nullptr;
    HelpDialogService* helpDialog = nullptr;
    UiStateCenter* stateCenter = nullptr;
    LayerPersistenceBridge* layerPersistence = nullptr;
    PersistenceService* persistence = nullptr;
    QWidget* parentWidget = nullptr;
};

/**
 * @brief 文件操作注册器 — 将文件导入/导出/保存/打开等操作注册到 OperationBus
 *
 * P5 收口：将公共模板抽取为私有方法，减少 registerAll() 中的内联 lambda 膨胀。
 * P3b 收敛：构造函数参数封装为 FileOperationConfig，导入/导出错误处理提取公共方法。
 */
class FileOperationRegistry
{
public:
    explicit FileOperationRegistry(const FileOperationConfig& config);

    void registerAll();

private:
    // ==================== 公共模板方法 ====================

    /// 保存文档记录到持久化层
    void saveDocumentRecord(const std::string& filePath, int entityCount);

    /// 执行导出流程（收集图元 → 分发写入 → 状态回写）
    bool doExport(const std::string& filePath);

    /// 执行打开文件流程（导入 → 状态回写）
    bool doOpenFile(const QString& filePath);

    /// 执行导入流程（按格式选文件 → 调用导入服务 → 异常保护）
    void doImportByFormat(Fio::FileFormat fmt);

    /// 执行导入图片流程（filePath 可为空：为空则弹文件对话框）
    void doImportImage(const QString& filePath);

    /// 执行导出流程（按格式选文件 → 调用导出服务 → 异常保护）
    void doExportByFormat(Fio::FileFormat fmt);

    // ==================== 公共辅助方法 ====================

    /// 统一显示文件操作错误对话框
    void showFileError(const QString& title, const QString& message);

    /// 统一异常保护包装器：执行操作并在异常时显示错误对话框
    void executeWithExceptionGuard(const char* operationName, std::function<void()> action);

    // ==================== 操作注册子方法 ====================

    void registerFileNewOps();
    void registerFileOpenOps();
    void registerFileSaveOps();
    void registerImportOps();
    void registerExportOps();

    /// 共享的文件路径状态（用于 File_Save 和 File_SaveAs 共用）
    std::shared_ptr<std::string> m_currentFilePath;

    OperationBus* m_bus;
    Eg::SceneManager* m_sceneManager;
    ImportService* m_importService;
    ExportService* m_exportService;
    RecentFileService* m_recentFiles;
    HelpDialogService* m_helpDialog;
    UiStateCenter* m_stateCenter;
    LayerPersistenceBridge* m_layerPersistence;
    PersistenceService* m_persistence;
    QWidget* m_parentWidget;
};
