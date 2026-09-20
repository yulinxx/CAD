#include "LayerRepository.h"

#include "Engine/Persistence/Database.h"
#include "Log/SyLogger.h"

LayerRepository::LayerRepository(Eg::Database& database)
    : SqliteRepositoryBase(database)
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
    auto values = recordToRow(record);
    std::map<std::string, std::string> whereParams;
    whereParams["document_id"] = record.documentId;
    whereParams["layer_id"] = std::to_string(record.layerId);

    // 先尝试更新，避免 N+1 查询（不再调用 loadByDocument 预加载所有图层）
    // 利用 changes() 判断是否存在已有记录，若存在则更新，否则插入
    if (!m_database.update("layers", values, "document_id = :document_id AND layer_id = :layer_id", whereParams))
    {
        return fail("LayerRepository", "Failed to save layer");
    }

    if (m_database.changes() > 0)
    {
        SY_DEBUGF("[LayerRepository] Updated layer: doc=%s, layer=%d", record.documentId.c_str(), record.layerId);
        return true;
    }

    // 无记录被更新，执行插入
    if (!m_database.insertOrReplace("layers", values))
    {
        return fail("LayerRepository", "Failed to save layer");
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
        return fail("LayerRepository", "Failed to remove layer");
    }

    SY_DEBUGF("[LayerRepository] Removed layer: doc=%s, layer=%d", documentId.c_str(), layerId);
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
        return fail("LayerRepository", "Failed to rename layer");
    }
    SY_DEBUGF("[LayerRepository] Renamed layer: doc=%s, layer=%d, name=%s", documentId.c_str(), layerId, newName.c_str());
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
        return fail("LayerRepository", "Failed to update layer visibility");
    }
    SY_DEBUGF("[LayerRepository] Updated layer visibility: doc=%s, layer=%d, visible=%d", documentId.c_str(), layerId, visible ? 1 : 0);
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
        return fail("LayerRepository", "Failed to update layer lock");
    }
    SY_DEBUGF("[LayerRepository] Updated layer lock: doc=%s, layer=%d, locked=%d", documentId.c_str(), layerId, locked ? 1 : 0);
    return true;
}

bool LayerRepository::updateFill(const std::string& documentId, int layerId, bool fill)
{
    // 按文档 + 图层 ID 更新填充图层标志
    std::map<std::string, std::string> values;
    values["fill"] = fill ? "1" : "0";
    std::map<std::string, std::string> whereParams;
    whereParams["document_id"] = documentId;
    whereParams["layer_id"] = std::to_string(layerId);
    if (!m_database.update("layers", values, "document_id = :document_id AND layer_id = :layer_id", whereParams))
    {
        return fail("LayerRepository", "Failed to update layer fill");
    }
    SY_DEBUGF("[LayerRepository] Updated layer fill: doc=%s, layer=%d, fill=%d", documentId.c_str(), layerId, fill ? 1 : 0);
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
        return fail("LayerRepository", "Failed to update layer color");
    }
    SY_DEBUGF("[LayerRepository] Updated layer color: doc=%s, layer=%d, color=%s", documentId.c_str(), layerId, color.c_str());
    return true;
}

bool LayerRepository::updateLayerType(const std::string& documentId, int layerId, int layerType)
{
    // 按文档 + 图层 ID 更新图层类型
    std::map<std::string, std::string> values;
    values["layer_type"] = std::to_string(layerType);
    std::map<std::string, std::string> whereParams;
    whereParams["document_id"] = documentId;
    whereParams["layer_id"] = std::to_string(layerId);
    if (!m_database.update("layers", values, "document_id = :document_id AND layer_id = :layer_id", whereParams))
    {
        return fail("LayerRepository", "Failed to update layer type");
    }
    SY_DEBUGF("[LayerRepository] Updated layer type: doc=%s, layer=%d, type=%d", documentId.c_str(), layerId, layerType);
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
            return fail("LayerRepository", "Failed to update order for layer " + std::to_string(layerId));
        }
    }

    if (!txn.commit())
    {
        return fail("LayerRepository", "Failed to commit batch order update");
    }

    SY_DEBUGF("[LayerRepository] Batch updated order for %zu layers", layerIdAndOrders.size());
    return true;
}

LayerRecord LayerRepository::rowToRecord(const std::map<std::string, std::string>& row) const
{
    LayerRecord rec;
    rec.id = getInt(row, "id");
    rec.documentId = getString(row, "document_id");
    rec.layerId = getInt(row, "layer_id");
    rec.name = getString(row, "name");
    rec.color = getString(row, "color", "#000000");
    rec.visible = getBool(row, "visible", true);
    rec.locked = getBool(row, "locked");
    rec.fill = getBool(row, "fill");
    rec.fillColor = getString(row, "fill_color");
    rec.layerType = getInt(row, "layer_type");
    rec.orderIndex = getInt(row, "order_index");
    rec.updatedAt = getString(row, "updated_at");
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
    row["fill"] = rec.fill ? "1" : "0";
    row["fill_color"] = rec.fillColor;
    row["layer_type"] = std::to_string(rec.layerType);
    row["order_index"] = std::to_string(rec.orderIndex);
    row["updated_at"] = rec.updatedAt;
    return row;
}