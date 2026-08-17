#pragma once

#include <string>

/**
 * @brief 图层记录 — 数据库持久化模型
 *
 * 保存图层的基本属性，与文档关联。LayerRepository 负责读写。
 * 注意：图层图元关联关系由 Engine 层的 LayerManager 管理，不在此模型范围。
 */
struct LayerRecord
{
    int id{ 0 };             // 自增主键
    std::string documentId;  // 所属文档 ID
    int layerId{ -1 };       // Engine 层图层 ID（LayerManager::createLayer 返回）
    std::string name;        // 图层名称
    std::string color;       // 图层颜色（hex 格式，如 "#FF0000"）
    bool visible{ true };    // 是否可见
    bool locked{ false };    // 是否锁定
    bool fill{ false };      // 是否填充图层（色块填充）
    std::string fillColor;   // 填充色（hex 格式，如 "#FF0000"；色块填充使用）
    int layerType{ 0 };      // 图层类型（0=矢量 VECTOR，1=位图 BITMAP）
    int orderIndex{ 0 };     // 图层排序序号
    std::string updatedAt;   // 最近更新时间（ISO 8601 格式）
};