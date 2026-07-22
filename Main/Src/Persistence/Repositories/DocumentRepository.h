#pragma once

#include "Persistence/Models/DocumentRecord.h"

#include <map>
#include <vector>
#include <string>

namespace Eg { class Database; }

/**
 * @brief 文档元数据仓储 — 封装 documents 表的 CRUD 操作
 *
 * 管理和持久化文档元数据信息，包括文档基本属性、大小、时间戳等。
 * 文件打开/保存时调用 save() 更新文档元数据，文件关闭时调用 remove()。
 * WorkbenchWindow 通过 PersistenceService 访问此仓储。
 */
class DocumentRepository
{
public:
    explicit DocumentRepository(Eg::Database& database);

    /// 加载所有已保存的文档元数据记录
    std::vector<DocumentRecord> loadAll();

    /// 根据文件路径加载单条文档元数据
    DocumentRecord loadByPath(const std::string& filePath);

    /// 保存或更新文档元数据（存在则更新，不存在则插入）
    bool save(const DocumentRecord& record);

    /// 根据文件路径删除文档元数据
    bool remove(const std::string& filePath);

    /// 最近一次操作的错误信息
    const std::string& lastError() const;

private:
    DocumentRecord rowToRecord(const std::map<std::string, std::string>& row) const;
    std::map<std::string, std::string> recordToRow(const DocumentRecord& rec) const;

    Eg::Database& m_database;
    std::string m_lastError;
};
