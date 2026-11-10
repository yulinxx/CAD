#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include <QString>
#include <QStringList>

#include "FileIO/FioTypes.h"
#include "FileIO/AprDocument.h"

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

    /// 源文件图层表（DXF 等支持图层的格式解析结果，供构建文档阶段重建图层）
    std::vector<Fio::IrLayerInfo> importedLayers;

    /// 图元的**加工语义**（与显示语义严格分离）
    ///
    /// 为什么不放在 `Fio::IrLayerInfo` 里：
    ///   - 它是冻结的跨 DLL POD（`FioParseResult::layers` 是指向 DLL 内部内存的
    ///     裸指针，步长即 ABI），加字段会改 sizeof；
    ///   - 而且 DXF / SVG / PLT 这些外来格式根本没有「是否输出」「默认工艺」的概念，
    ///     把它塞进通用导入 IR 是为一两种格式的便利污染所有格式的公共契约。
    ///
    /// 只有原生 .apr 有加工语义，所以它走这条独立通道。
    /// `sourceId` 对应 `importedLayers[i].sourceId`。
    struct LayerMachining
    {
        uint32_t sourceId = 0;
        bool output = true;             // 是否参与加工输出
        uint32_t defaultProcessId = 0;  // 默认工艺 ID，0 = 未指定

        // ---- 原生 .apr 的硬件 Profile 绑定（只是 id 引用，值集见下）----
        // 0 = 默认 Profile；fill 为 -1 表示未设置、回退到 hardwareProfileId。
        int32_t hardwareProfileId = 0;
        int32_t fillHardwareProfileId = -1;

        // ---- 原生 .apr 的显示扩展（IrLayerInfo 是冻结 POD，不能加字段）----
        // hasDisplayExtension=false 时下面三项无效，读端必须保持现有图层显示设置。
        bool hasDisplayExtension = false;
        uint32_t layerType = 0;         // 0=VECTOR, 1=BITMAP
        bool fill = false;
        uint32_t fillColor = 0xFFFFFFFF;
    };
    std::vector<LayerMachining> importedLayerMachining;

    /// 原生 .apr 图层绑定所指向的**参数值集**。
    ///
    /// 只有 id 引用没有值集，等于参数没存：重开文件时引用会落到运行时默认
    /// Profile 上。空 = 旧格式文件（v2 之前不写本字段），读端必须**保持**
    /// 运行时现有 Profile，绝不能清空或重置用户的工艺参数。
    /// 元素直接复用 FileIO 的跨 DLL POD，避免 Main 侧再定义一套重复结构。
    std::vector<Fio::AprHardwareProfileInfo> importedHardwareProfiles;

    /// 这份文件是否带了完整的原生加工语义快照（图层表 + 非空 Profile 值集）。
    /// true 时即便图层按名复用，也要用文件里的加工语义覆盖运行时设置；
    /// false 时沿用 DXF 等外来格式「同名图层保留用户设置」的旧行为。
    bool hasNativeMachiningSnapshot = false;

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
