#pragma once

#include <atomic>
#include <functional>
#include <vector>
#include <QString>
#include <QStringList>

#include "FileIO/FileFormat.h"
#include "FileIO/AprDocument.h"

namespace Eg
{
    class SceneManager;
    class SceneManager3D;
}

/// 原生 .apr 的「加工语义快照」。
///
/// 为什么是值快照而不是 LayerManager/HardwareProfileManager 指针：
///   - 导出异步执行，后台线程不允许访问主线程持有的 Manager；
///   - 快照在 Phase 1（主线程）采集后随 ExportContext 按值传进后台线程，
///     里面只有 POD，没有指针，线程安全；
///   - 元素直接复用 FileIO 的跨 DLL POD，避免再定义一套重复结构体、
///     或让 Main 去包含 FileIO 的内部头 AprDocumentData.h。
struct NativeDocumentSnapshot
{
    std::vector<Fio::AprLayerDetailInfo> layers;
    std::vector<Fio::AprHardwareProfileInfo> profiles;
};

/// 统一导出上下文：承载导出操作所需的所有参数和状态信息
struct ExportContext
{
    /// 目标文件完整路径
    QString targetPath;
    /// 导出格式
    Fio::FileFormat format{ Fio::FileFormat::Unknown };
    /// 源文档 ID（用于元数据记录）
    QString sourceDocumentId;
    /// 是否包含图层信息
    bool includeLayers{ true };
    /// 是否包含元数据
    bool includeMetadata{ true };
    /// 是否只导出选中内容
    bool includeSelectionOnly{ false };
    /// 导出 DPI（位图格式适用）
    int dpi{ 300 };
    /// 缩放因子
    double scaleFactor{ 1.0 };
    /// 页面尺寸（PDF 等格式适用）
    QString pageSize;
    /// 是否保留源颜色
    bool preserveColors{ true };
    /// 是否保留文本
    bool preserveText{ true };
    /// 附加元数据（扩展用）
    QStringList warnings;

    /// 场景管理器指针（用于 Native 格式导出时借用实体，避免深拷贝）
    /// 为 nullptr 时导出服务会使用 clone 方式（向后兼容）
    Eg::SceneManager* sceneManager{ nullptr };
    /// 3D 场景管理器指针（用于 3D 网格图元借用）
    Eg::SceneManager3D* sceneManager3D{ nullptr };

    /// 是否携带了有效的加工语义快照。
    /// 组合根没注入快照提供器时保持 false：NativeExportWriter 必须继续给出
    /// 「图层/工艺/Profile 未保存」的 warning，不能让操作员以为参数已落盘。
    bool hasMachiningSnapshot{ false };
    /// 图层表 + 硬件 Profile 值集的值快照（仅原生 .apr 使用）
    NativeDocumentSnapshot machiningSnapshot;

    /// 进度回调（可选，导出过程中报告进度 0.0~1.0）
    std::function<void(float progress, const char* stage)> progressCallback;

    /// 取消标志指针（可选，后台线程 writer 内部定期检查；nullptr 表示无法取消）
    /// 由 ExportService::exportAsync 注入 &m_asyncCancelFlag；writer 不得释放此指针
    std::atomic<bool>* cancelFlag{ nullptr };
};
