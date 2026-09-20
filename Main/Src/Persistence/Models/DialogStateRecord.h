#pragma once

#include <string>

/**
 * @brief 对话框状态记录 — 数据库持久化模型
 *
 * 用于保存任意对话框的界面状态数据（JSON 序列化）。
 * 由 DialogStateRepository 进行读写。
 */
struct DialogStateRecord
{
    int id{ 0 };                // 自增主键
    std::string dialogKey;      // 对话框唯一标识（如 "LayerEditDialog"）
    std::string documentId;     // 关联文档ID（可选，空表示全局状态）
    std::string stateJson;      // 对话框状态的 JSON 序列化字符串
    std::string createdAt;      // 创建时间（ISO 8601 格式）
    std::string updatedAt;      // 最近更新时间（ISO 8601 格式）
};
