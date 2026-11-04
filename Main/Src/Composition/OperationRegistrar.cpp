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

// HardwareProfileManager + LayerManager：注入后 specProvider 才能读取图层工艺参数
#include "Engine2D/Interaction/HardwareProfileManager.h"
#include "Engine2D/Interaction/LayerManager.h"
#include "Engine/Process/ProcessParams.h"

// UiShellHost::mainWindow() 返回 WorkbenchWindow*，转 QWidget* 需要完整类型
#include "UI/Workbench/WorkbenchWindow.h"

#include <QMessageBox>

#include "Log/SyLogger.h"

namespace {

/// @brief 把旧版 HardwareProfile 转成新版 Eg::ProcessParams，
///        单位换算：HardwareProfile.frequency 是 kHz → ProcessParams.frequencyHz 是 Hz
Eg::ProcessParams hwProfileToProcessParams(const HardwareProfile& hp)
{
    Eg::ProcessParams p;
    p.powerPercent = hp.power;
    p.speedMmPerSec = hp.speed;
    p.frequencyHz = hp.frequency * 1000.0;
    p.passes = hp.passes;
    p.focusOffsetMm = hp.focusOffset;
    // HardwareProfile 无填充/补偿/使能/脉宽/波形字段，保持 ProcessParams 默认值
    return p;
}

} // namespace

OperationRegistrar::OperationRegistrar(ApplicationCompositionRoot& root, AlgorithmRunner* algorithmRunner)
    : m_root(root)
    , m_algorithmRunner(algorithmRunner)
{
    // 实例化 HardwareProfileManager 并注入 LayerManager，使 layerHardwareProfile()
    // 能返回非空值；不注入则 LayerManager 会返回默认 HardwareProfile，specProvider
    // 也读不到任何用户设置的工艺参数
    m_hwProfileManager = std::make_unique<HardwareProfileManager>();
    if (root.layerManager())
    {
        root.layerManager()->setHardwareProfileManager(m_hwProfileManager.get());
    }
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
    laserConfig.safetyController = m_root.emergencyStopController();
    laserConfig.sceneManager = m_root.sceneManager();
    laserConfig.layerManager = m_root.layerManager();
    laserConfig.errorReporter = [host = m_root.shellHost()](const QString& message) {
        // 加工失败必须让操作员看见：只写日志的话，
        // 现场表现是「按了开始加工什么都没发生」
        QMessageBox::warning(host ? host->mainWindow() : nullptr, QObject::tr("Processing"), message);
    };
    // 作业规格：把机器坐标系从设备档案注入，同时从 LayerManager +
    // HardwareProfileManager 读取各图层工艺参数（功率/速度/遍数），
    // 避免用户在 UI 里设置的参数在加工时被静默忽略。
    laserConfig.specProvider = [this, dev = m_root.deviceHost(),
                                layerManager = m_root.layerManager()] {
        ToolpathJobSpec spec;
        spec.machineFrame = dev ? dev->machineFrameConfig() : Eg::MachineFrameConfig{};

        if (layerManager)
        {
            const auto layerIds = layerManager->getAllLayerIds();
            for (int layerId : layerIds)
            {
                const auto hp = layerManager->layerHardwareProfile(layerId);
                spec.layerParams.insert(layerId, hwProfileToProcessParams(hp));
            }
        }

        // defaultParams 取第一个图层的参数（如果有），否则 ProcessParams 默认值
        if (!spec.layerParams.isEmpty())
        {
            spec.defaultParams = spec.layerParams.first();
        }
        return spec;
    };

    m_laserOperationRegistry = std::make_unique<LaserOperationRegistry>(laserConfig);
    m_laserOperationRegistry->registerAll();

    // 占位操作（视图缩放等）由 PendingOperationRegistry 统一管理
    PendingOperationRegistry pendingOps(m_root.operationBus());
    pendingOps.registerAll();
}
