#include "LayerRepository.h"

#include "Engine/Persistence/Database.h"
#include "Log/SyLogger.h"

LayerRepository::LayerRepository(Eg::Database& database)
    : m_database(database)
{
}

std::vector<LayerRecord> LayerRepository::loadByDocument(const std::string& documentId)
{
    std::vector<LayerRecord> result;

    std::string sql = "SELECT * FROM layers WHERE document_id = ? ORDER BY order_index ASC";
    std::vector<std::string> params = { documentId };
    auto rows = m_database.query(sql, params);

    for (const auto& row : rows)
    {
        result.push_back(rowToRecord(row));
    }

    return result;
}

bool LayerRepository::save(const LayerRecord& record)
{
    // 先检查是否已存在同文档+同图层ID的记录，若存在则更新而非插入
    auto existingLayers = loadByDocument(record.documentId);
    bool exists = false;
    for (const auto& existing : existingLayers)
    {
        if (existing.layerId == record.layerId)
        {
            exists = true;
            break;
        }
    }

    if (exists)
    {
        // 已有记录：通过 (document_id, layer_id) 组合更新字段
        std::map<std::string, std::string> values;
        values["name"] = record.name;
        values["color"] = record.color;
        values["visible"] = record.visible ? "1" : "0";
        values["locked"] = record.locked ? "1" : "0";
        values["order_index"] = std::to_string(record.orderIndex);
        if (!record.updatedAt.empty())
        {
            values["updated_at"] = record.updatedAt;
        }

        std::map<std::string, std::string> whereParams;
        whereParams["document_id"] = record.documentId;
        whereParams["layer_id"] = std::to_string(record.layerId);

        if (!m_database.update("layers", values, "document_id = :document_id AND layer_id = :layer_id", whereParams))
        {
            m_lastError = "Failed to update layer: " + m_database.lastError();
            SY_ERRORF("[LayerRepository] %s", m_lastError.c_str());
            return false;
        }

        SY_DEBUGF("[LayerRepository] Updated layer: doc=%s, layer=%d", record.documentId.c_str(), record.layerId);
        return true;
    }

    // 新记录：插入
    auto values = recordToRow(record);
    if (!m_database.insertOrReplace("layers", values))
    {
        m_lastError = "Failed to save layer: " + m_database.lastError();
        SY_ERRORF("[LayerRepository] %s", m_lastError.c_str());
        return false;
    }

    SY_DEBUGF("[LayerRepository] Inserted layer: doc=%s, layer=%d", record.documentId.c_str(), record.layerId);
    return true;
}

bool LayerRepository::remove(const std::string& documentId, int layerId)
{
    std::map<std::string, std::string> whereParams;
    whereParams["document_id"] = documentId;
    whereParams["layer_id"] = std::to_string(layerId);

    if (!m_database.deleteRows("layers", "document_id = :document_id AND layer_id = :layer_id", whereParams))
    {
        m_lastError = "Failed to remove layer: " + m_database.lastError();
        SY_ERRORF("[LayerRepository] %s", m_lastError.c_str());
        return false;
    }

    return true;
}

std::vector<int> LayerRepository::listByDocument(const std::string& documentId)
{
    std::vector<int> result;

    std::string sql = "SELECT layer_id FROM layers WHERE document_id = ? ORDER BY order_index ASC";
    std::vector<std::string> params = { documentId };
    auto rows = m_database.query(sql, params);

    for (const auto& row : rows)
    {
        auto it = row.find("layer_id");
        if (it != row.end())
        {
            result.push_back(std::stoi(it->second));
        }
    }

    return result;
}

bool LayerRepository::rename(const std::string& documentId, int layerId, const std::string& newName)
{
    // 按文档 + 图层 ID 更新名称
    std::map<std::string, std::string> values;
    values["name"] = newName;
    std::map<std::string, std::string> whereParams;
    whereParams["document_id"] = documentId;
    whereParams["layer_id"] = std::to_string(layerId);
    if (!m_database.update("layers", values, "document_id = :document_id AND layer_id = :layer_id", whereParams))
    {
        m_lastError = "Failed to rename layer: " + m_database.lastError();
        SY_ERRORF("[LayerRepository] %s", m_lastError.c_str());
        return false;
    }
    return true;
}

bool LayerRepository::updateVisibility(const std::string& documentId, int layerId, bool visible)
{
    // 按文档 + 图层 ID 更新可见性
    std::map<std::string, std::string> values;
    values["visible"] = visible ? "1" : "0";
    std::map<std::string, std::string> whereParams;
    whereParams["document_id"] = documentId;
    whereParams["layer_id"] = std::to_string(layerId);
    if (!m_database.update("layers", values, "document_id = :document_id AND layer_id = :layer_id", whereParams))
    {
        m_lastError = "Failed to update layer visibility: " + m_database.lastError();
        SY_ERRORF("[LayerRepository] %s", m_lastError.c_str());
        return false;
    }
    return true;
}

bool LayerRepository::updateLocked(const std::string& documentId, int layerId, bool locked)
{
    // 按文档 + 图层 ID 更新锁定状态
    std::map<std::string, std::string> values;
    values["locked"] = locked ? "1" : "0";
    std::map<std::string, std::string> whereParams;
    whereParams["document_id"] = documentId;
    whereParams["layer_id"] = std::to_string(layerId);
    if (!m_database.update("layers", values, "document_id = :document_id AND layer_id = :layer_id", whereParams))
    {
        m_lastError = "Failed to update layer lock: " + m_database.lastError();
        SY_ERRORF("[LayerRepository] %s", m_lastError.c_str());
        return false;
    }
    return true;
}

bool LayerRepository::updateColor(const std::string& documentId, int layerId, const std::string& color)
{
    // 按文档 + 图层 ID 更新颜色
    std::map<std::string, std::string> values;
    values["color"] = color;
    std::map<std::string, std::string> whereParams;
    whereParams["document_id"] = documentId;
    whereParams["layer_id"] = std::to_string(layerId);
    if (!m_database.update("layers", values, "document_id = :document_id AND layer_id = :layer_id", whereParams))
    {
        m_lastError = "Failed to update layer color: " + m_database.lastError();
        SY_ERRORF("[LayerRepository] %s", m_lastError.c_str());
        return false;
    }
    return true;
}

bool LayerRepository::batchUpdateOrder(
    const std::string& documentId, const std::vector<std::pair<int, int>>& layerIdAndOrders)
{
    Eg::Database::Transaction txn(m_database);

    for (const auto& [layerId, orderIndex] : layerIdAndOrders)
    {
        std::map<std::string, std::string> values;
        values["order_index"] = std::to_string(orderIndex);
        std::map<std::string, std::string> whereParams;
        whereParams["document_id"] = documentId;
        whereParams["layer_id"] = std::to_string(layerId);
        if (!m_database.update("layers", values, "document_id = :document_id AND layer_id = :layer_id", whereParams))
        {
            m_lastError = "Failed to update order for layer " + std::to_string(layerId) + ": " + m_database.lastError();
            SY_ERRORF("[LayerRepository] %s", m_lastError.c_str());
            return false;
        }
    }

    if (!txn.commit())
    {
        m_lastError = "Failed to commit batch order update: " + m_database.lastError();
        SY_ERRORF("[LayerRepository] %s", m_lastError.c_str());
        return false;
    }

    SY_DEBUGF("[LayerRepository] Batch updated order for %zu layers", layerIdAndOrders.size());
    return true;
}

const std::string& LayerRepository::lastError() const
{
    return m_lastError;
}

LayerRecord LayerRepository::rowToRecord(const std::map<std::string, std::string>& row) const
{
    LayerRecord rec;
    auto it = row.find("id");
    if (it != row.end())
    {
        rec.id = std::stoi(it->second);
    }
    it = row.find("document_id");
    if (it != row.end())
    {
        rec.documentId = it->second;
    }
    it = row.find("layer_id");
    if (it != row.end())
    {
        rec.layerId = std::stoi(it->second);
    }
    it = row.find("name");
    if (it != row.end())
    {
        rec.name = it->second;
    }
    it = row.find("color");
    if (it != row.end())
    {
        rec.color = it->second;
    }
    it = row.find("visible");
    if (it != row.end())
    {
        rec.visible = (it->second == "1");
    }
    it = row.find("locked");
    if (it != row.end())
    {
        rec.locked = (it->second == "1");
    }
    it = row.find("order_index");
    if (it != row.end())
    {
        rec.orderIndex = std::stoi(it->second);
    }
    it = row.find("updated_at");
    if (it != row.end())
    {
        rec.updatedAt = it->second;
    }
    return rec;
}

std::map<std::string, std::string> LayerRepository::recordToRow(const LayerRecord& rec) const
{
    std::map<std::string, std::string> row;
    if (rec.id > 0)
    {
        row["id"] = std::to_string(rec.id);
    }
    row["document_id"] = rec.documentId;
    row["layer_id"] = std::to_string(rec.layerId);
    row["name"] = rec.name;
    row["color"] = rec.color;
    row["visible"] = rec.visible ? "1" : "0";
    row["locked"] = rec.locked ? "1" : "0";
    row["order_index"] = std::to_string(rec.orderIndex);
    row["updated_at"] = rec.updatedAt;
    return row;
}