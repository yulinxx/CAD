#pragma once

#include <string>

/**
 * @brief 文档元数据记录 — 数据库持久化模型
 *
 * 保存文档的基本元信息，包括文件名、路径、格式、大小、修改时间等。
 * 由 DocumentRepository 进行读写，WorkbenchWindow 在文件打开/保存时读写。
 * 注意：文档实体内容不由此模型管理，实体数据通过 FileIOManager 导入导出。
 */
struct DocumentRecord
{
    int id{ 0 };                    // 自增主键
    std::string filePath;           // 文件完整路径
    std::string title;              // 文件标题（不含路径）
    std::string format;             // 文件格式（如 DXF, SVG, SY）
    int entityCount{ 0 };           // 文件中实体数量
    std::string fileSize;           // 文件大小（格式化字符串）
    std::string lastOpenedAt;       // 最后打开时间（ISO 8601 格式）
    std::string lastSavedAt;        // 最后保存时间（ISO 8601 格式）
    std::string createdAt;          // 创建时间（ISO 8601 格式）
};
