#pragma once

#include "Persistence/Models/LayerRecord.h"

#include <map>
#include <vector>
#include <string>

namespace Eg
{
    class Database;
}

/**
 * @brief 图层仓储 — 封装 layers 表的 CRUD 操作
 *
 * 按文档ID隔离图层数据，支持批量读写。
 * UiWorkbench 通过 PersistenceService 访问此仓储。
 */
class LayerRepository
{
public:
    explicit LayerRepository(Eg::Database& database);

    /// 加载指定文档的所有图层（按 order_index 排序）
    std::vector<LayerRecord> loadByDocument(const std::string& documentId);

    /// 保存或更新图层记录
    bool save(const LayerRecord& record);

    /// 删除指定文档的指定图层
    bool remove(const std::string& documentId, int layerId);

    /// 列出指定文档的所有图层 ID
    std::vector<int> listByDocument(const std::string& documentId);

    /// 图层字段级更新
    bool rename(const std::string& documentId, int layerId, const std::string& newName);
    bool updateVisibility(const std::string& documentId, int layerId, bool visible);
    bool updateLocked(const std::string& documentId, int layerId, bool locked);
    bool updateColor(const std::string& documentId, int layerId, const std::string& color);

    /// 批量更新图层顺序（在一个事务中完成）
    bool batchUpdateOrder(const std::string& documentId, const std::vector<std::pair<int, int>>& layerIdAndOrders);

    const std::string& lastError() const;

private:
    LayerRecord rowToRecord(const std::map<std::string, std::string>& row) const;
    std::map<std::string, std::string> recordToRow(const LayerRecord& rec) const;

    Eg::Database& m_database;
    std::string m_lastError;
};