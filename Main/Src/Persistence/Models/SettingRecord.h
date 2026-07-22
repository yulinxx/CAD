#pragma once

#include <string>

/**
 * @brief 设置记录 — 数据库持久化模型
 *
 * 通用键值设置，支持分组和类型标记。
 * 由 SettingsRepository 读写，UI 偏好、主题、语言等设置通过此模型持久化。
 */
struct SettingRecord
{
    int id{ 0 };                    // 自增主键
    std::string groupName;          // 设置分组（如 "ui", "viewport", "file"）
    std::string key;                // 设置键名
    std::string value;              // 设置值（字符串形式存储）
    std::string dataType;           // 值类型标记（"string", "int", "bool", "double"）
    std::string updatedAt;          // 最近更新时间（ISO 8601 格式）
};