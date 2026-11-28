#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include <QString>
#include <QStringList>

#include "FileIO/FioTypes.h"
#include "FileIO/AprDocument.h"

#include "Engine/Process/ProcessModel.h"

/// 导入错误类型：区分不同的失败原因，便于上层展示和处理
enum class ImportErrorType
{
    None,                          ///< 无错误（成功）
    FileNotFound,                  ///< 文件不存在或路径无效
    FormatNotSupported,            ///< 文件格式不支持
    ParseFailed,                   ///< 解析失败（文件损坏、格式错误）
    UnitIncompatible,              ///< 单位不兼容
    CoordinateSystemIncompatible,  ///< 坐标系不兼容
    Canceled,                      ///< 用户取消导入
    Unknown                        ///< 未知错误
};

/// 导入结果：封装导入操作的结果状态和统计信息
struct ImportResult
{
    /// 导入是否成功
    bool success{ false };
    /// 错误类型（失败时有效）
    ImportErrorType errorType{ ImportErrorType::None };
    /// 主消息（成功或失败描述）
    QString message;
    /// 警告列表（解析过程中的非致命问题）
    QStringList warnings;
    /// 导入的图元数量
    int entityCount{ 0 };
    /// 导入的节点数量（3D 模型树节点）
    int nodeCount{ 0 };
    /// 导入的图层数量
    int layerCount{ 0 };
    /// 导入后使用的工作台 ID
    QString usedWorkbenchId;
    /// 本次导入是否已清空过当前场景（importAsNewDocument=true 时才会发生）。
    /// 失败结果里为 true 表示「旧文档已经没了、新文档也没进来」：调用方必须把文档状态
    /// 复位为未命名（清当前路径 / 撤销栈 / 脏标记），否则一次保存就会用空场景覆盖上一份文件。
    bool documentCleared{ false };

    /// 源文件图层表（DXF 等支持图层的格式解析结果，供构建文档阶段重建图层）
    std::vector<Fio::IrLayerInfo> importedLayers;

    /// 原生 .apr 的完整图层表（显示属性 + 加工语义 + 硬件 Profile 绑定），按文件顺序。
    /// 打开 .apr 时图层表按它原样重建（图层 id 不变）；外来格式为空。
    std::vector<Fio::AprLayerInfo> nativeLayers;

    /// 原生 .apr 的硬件 Profile 值集（图层上的 profile id 引用指向它）。
    /// 打开 .apr 时整体替换运行时 Profile；为空则重置为默认 Profile。
    std::vector<Fio::AprHardwareProfileInfo> importedHardwareProfiles;

    /// 原生 .apr 的工艺对象（ProcessData）。
    ///
    /// 参数体不能只靠图层表还原：ProcessData 才是参数的一等载体，
    /// 图层表里的 default_process_id 只是反向绑定。这里把参数体与显式目标图层
    /// 一起带出来，由 ImportService 在图层落地后按图层 ID 回灌。
    struct ProcessBinding
    {
        Fio::AprProcessInfo info;                  ///< id/name/order/output/params
        std::vector<uint32_t> targetLayerIds;      ///< 显式目标图层（文档图层 ID）
    };
    std::vector<ProcessBinding> importedProcesses;

    /// 原生 .apr 的自定义加工顺序（文档图层 ID，按加工先后）。
    /// 空 = 从未调整过：打开时加工顺序跟随显示顺序。
    std::vector<uint32_t> importedMachiningLayerOrder;

    /// 原生 .apr 的工艺细节模型（ProcessModelData）。
    ///
    /// ProcessData 只承载基础参数；割缝/引线/穿孔/共边/功率曲线等细节在
    /// ProcessModel 里。文件没有该块或外来格式：hasProcessModel=false，
    /// 细节一律按「继承/关闭」，不得用默认值填充。
    Eg::ProcessModel processModel;
    bool hasProcessModel = false;

    /// 图元图层归属映射：转换后图元的 EntityId(int64) → 源图层 sourceId
    /// 与 importedLayers 配合，在构建文档阶段将图元还原到对应图层
    std::unordered_map<int64_t, uint32_t> entityLayerMap;

    /// 源文件群组表（DXF 块引用 / SVG <g> / OBJ 的 o-g-usemtl 分段），
    /// 父子关系由 IrGroupInfo::parentSourceId 表达，0 表示顶层
    std::vector<Fio::IrGroupInfo> importedGroups;
    /// 图元群组归属映射：转换后图元的 EntityId(int64) → 源群组 sourceId
    /// 与 importedGroups 配合，在构建文档阶段重建 AprGroup 树
    std::unordered_map<int64_t, uint64_t> entityGroupMap;


    /// 创建成功结果
    static ImportResult ok(
        const QString& msg = QString(), int entities = 0, int layers = 0, const QStringList& warns = {})
    {
        ImportResult r;
        r.success = true;
        r.errorType = ImportErrorType::None;
        r.message = msg;
        r.warnings = warns;
        r.entityCount = entities;
        r.layerCount = layers;
        return r;
    }

    /// 创建失败结果（带错误类型）
    static ImportResult fail(
        const QString& msg, ImportErrorType errorType = ImportErrorType::Unknown, const QStringList& warns = {})
    {
        ImportResult r;
        r.success = false;
        r.errorType = errorType;
        r.message = msg;
        r.warnings = warns;
        return r;
    }

    /// 追加警告
    void addWarning(const QString& warn)
    {
        warnings.append(warn);
    }
};
