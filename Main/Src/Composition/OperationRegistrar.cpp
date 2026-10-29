/**
 * @file OperationRegistrar.cpp
 * @brief 操作注册器实现（从 ApplicationCompositionRoot::registerAllOperations 提取）
 */
#include "OperationRegistrar.h"

#include "ApplicationCompositionRoot.h"
#include "CoreOperationRegistry.h"
#include "FileOperationRegistry.h"
#include "LaserOperationRegistry.h"
#include "PendingOperationRegistry.h"

#include "../Hardware/DeviceHost.h"

// UiShellHost::mainWindow() 返回 WorkbenchWindow*，转 QWidget* 需要完整类型
#include "UI/Workbench/WorkbenchWindow.h"

#include <QMessageBox>

#include "Log/SyLogger.h"

OperationRegistrar::OperationRegistrar(ApplicationCompositionRoot& root, AlgorithmRunner* algorithmRunner)
    : m_root(root)
    , m_algorithmRunner(algorithmRunner)
{
}

OperationRegistrar::~OperationRegistrar() = default;

void OperationRegistrar::registerAll()
{
    SY_INFO("[OperationRegistrar] registering module operations on OperationBus");

    // 核心操作（撤销/重做/删除/圆角/倒角/选择/帮助 + 编辑操作 + 算法操作 + 视图操作）
    CoreOperationRegistry coreOps(m_root.operationBus(),
        m_root.sceneEditService(),
        m_root.undoRedoManager(),
        m_root.clipboard(),
        m_algorithmRunner,
        m_root.viewportActionHub(),
        m_root.stateCenter(),
        m_root.layerEditService(),
        m_root.unitManager(),
        m_root.shellHost() ? m_root.shellHost()->mainWindow() : nullptr,
        m_root.uiServices().captureService);

    coreOps.registerAll();

    // 文件操作（导入/导出/打开/保存）
    FileOperationConfig fileConfig;
    fileConfig.bus = m_root.operationBus();
    fileConfig.sceneManager = m_root.sceneManager();
    fileConfig.layerManager = m_root.layerManager();
    fileConfig.importService = m_root.importService();
    fileConfig.exportService = m_root.exportService();
    fileConfig.recentFiles = m_root.uiServices().recentFileService;
    fileConfig.helpDialog = m_root.helpDialogService();
    fileConfig.stateCenter = m_root.stateCenter();
    fileConfig.layerPersistence = m_root.layerPersistenceBridge();
    fileConfig.persistence = m_root.persistenceService();
    fileConfig.parentWidget = m_root.shellHost() ? m_root.shellHost()->mainWindow() : nullptr;
    // 新建/打开时清空撤销栈：跨文档撤销会把上一份文档的图元插回新文档
    fileConfig.undoRedoManager = m_root.undoRedoManager();

    m_fileOperationRegistry = std::make_unique<FileOperationRegistry>(fileConfig);
    m_fileOperationRegistry->registerAll();

    // 加工操作（开始/暂停/停止/急停）
    LaserOperationConfig laserConfig;
    laserConfig.bus = m_root.operationBus();
    laserConfig.deviceHost = m_root.deviceHost();
    laserConfig.jobService = m_root.processingJobService();
    laserConfig.sceneManager = m_root.sceneManager();
    laserConfig.layerManager = m_root.layerManager();
    laserConfig.errorReporter = [host = m_root.shellHost()](const QString& message) {
        // 加工失败必须让操作员看见：只写日志的话，
        // 现场表现是「按了开始加工什么都没发生」
        QMessageBox::warning(host ? host->mainWindow() : nullptr, QObject::tr("Processing"), message);
    };
    // 作业规格：把**机器坐标系**从设备档案注入。
    //
    // 换算只在编译器的最后一步发生（MotionPlanCompiler），配置必须从这里一路传下去。
    // 之前这条链路是断的：UI2D 旧链路在服务层换算，新 HAL 链路完全不换算 ——
    // 同一份文档走不同路径会烧到不同位置。
    laserConfig.specProvider = [dev = m_root.deviceHost()] {
        ToolpathJobSpec spec;
        spec.machineFrame = dev ? dev->machineFrameConfig() : Eg::MachineFrameConfig{};
        return spec;
    };

    m_laserOperationRegistry = std::make_unique<LaserOperationRegistry>(laserConfig);
    m_laserOperationRegistry->registerAll();

    // 占位操作（视图缩放等）由 PendingOperationRegistry 统一管理
    PendingOperationRegistry pendingOps(m_root.operationBus());
    pendingOps.registerAll();
}
