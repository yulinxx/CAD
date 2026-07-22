#pragma once

#include <string>

/**
 * @brief 最近文件记录 — 数据库持久化模型
 *
 * 记录用户最近打开的文件路径、标题、格式和最后访问时间。
 * 由 RecentFileRepository 进行读写，WorkbenchWindow 消费。
 */
struct RecentFileRecord
{
    int id{ 0 };                    // 自增主键
    std::string filePath;           // 文件完整路径
    std::string title;              // 文件标题（不含路径）
    std::string format;             // 文件格式（如 DXF, PLT, SVG）
    std::string lastOpenedTime;     // 最后打开时间（ISO 8601 格式）
};