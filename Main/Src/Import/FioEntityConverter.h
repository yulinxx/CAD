#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "FileIO/FioTypes.h"

namespace Eg
{
    struct SyEntity;
}

// ============================================================================
// FioEntityConverter — FileIO 中立 IR → Engine SyEntity 转换器
//
// 职责：
//   将 Fio::EntityInfo / Fio::ParseData（中立 POD）转换为 Eg::SyEntity（领域对象）
//
// 设计：
//   这是 FileIO 与 Engine 之间的"转换层"。
//   FileIO 解析器只输出中立 IR，不感知 Engine 类型。
//   转换层负责 IR → 领域对象的实例化，实现依赖方向的正确性。
//
// 使用方式：
//   Fio::ParseData parseData = ...;  // 从 FileIO 解析器获取
//   auto entities = FioEntityConverter::convert(parseData);
//   // entities 可直接添加到 SceneManager
// ============================================================================
class FioEntityConverter
{
public:
    /// 将单个 EntityInfo 转换为 SyEntity（不含扩展数据）
    /// @param info 中立图元信息
    /// @return 领域图元对象，失败返回 nullptr
    static std::unique_ptr<Eg::SyEntity> convertEntity(const Fio::EntityInfo& info);

    /// 将单个 EntityInfo 转换为 SyEntity（含扩展数据块）
    /// @param info 中立图元信息
    /// @param extensionBlob 扩展数据块（多边形顶点、NURBS控制点等）
    /// @return 领域图元对象，失败返回 nullptr
    static std::unique_ptr<Eg::SyEntity> convertEntity(
        const Fio::EntityInfo& info, const Fio::BinaryBlob& extensionBlob);

    /// 将 FioParseResult 中的所有图元批量转换为 SyEntity 列表
    /// @param parseData 解析结果（中立 IR）
    /// @return 领域图元对象列表
    static std::vector<std::unique_ptr<Eg::SyEntity>> convertAll(const Fio::FioParseResult& parseData);

    /// 将 FioParseResult 中的所有图元批量转换为 SyEntity 列表，并输出图层归属映射
    /// @param parseData 解析结果（中立 IR）
    /// @param outEntityLayerMap 可选输出：转换后图元 EntityId(int64) → 源图层 sourceId（仅 layerSourceId != 0 的图元）
    /// @return 领域图元对象列表
    static std::vector<std::unique_ptr<Eg::SyEntity>> convertAll(
        const Fio::FioParseResult& parseData, std::unordered_map<int64_t, uint32_t>* outEntityLayerMap);

    /// 将 FioParseResult 中的图层信息提取为独立列表
    /// @param parseData 解析结果
    /// @return 图层信息列表（调用方自行管理生命周期）
    static std::vector<Fio::IrLayerInfo> extractLayers(const Fio::FioParseResult& parseData);

private:
    /// 从 EntityInfo 的扩展数据中读取顶点数组
    static std::vector<Fio::Point2D> readExtensionPoints(const Fio::EntityInfo& info, const Fio::BinaryBlob& blob);
};