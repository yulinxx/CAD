#include "LayerPersistenceBridge.h"

#include "Repositories/LayerRepository.h"
#include "Engine2D/Interaction/LayerManager.h"
#include "Engine/EngineAPI.h"
#include "Log/SyLogger.h"

#include <QDateTime>

LayerPersistenceBridge::LayerPersistenceBridge(
    LayerManager* layerManager, LayerRepository* layerRepository)
    : m_layerManager(layerManager)
    , m_layerRepository(layerRepository)
{
}

void LayerPersistenceBridge::setDocumentId(const std::string& documentId)
{
    m_documentId = documentId;
}

const std::string& LayerPersistenceBridge::documentId() const
{
    return m_documentId;
}

/// 开始监听图层变更事件
void LayerPersistenceBridge::attach()
{
    if (m_attached || !m_layerManager)
        return;
    m_layerManager->addObserver(this);
    m_attached = true;
    SY_INFO("[LayerPersistenceBridge] Attached to LayerManager");
}

/// 停止监听图层变更事件
void LayerPersistenceBridge::detach()
{
    if (!m_attached || !m_layerManager)
        return;
    m_layerManager->removeObserver(this);
    m_attached = false;
    SY_INFO("[LayerPersistenceBridge] Detached from LayerManager");
}

/// 图层新增时同步写入数据库
void LayerPersistenceBridge::onLayerAdded(int nLayerId, const std::string& name)
{
    SY_INFOF("[LayerPersistenceBridge] Layer added: id=%d, name=%s", nLayerId, name.c_str());
    syncLayerToDb(nLayerId);
}

/// 图层删除时同步从数据库移除
void LayerPersistenceBridge::onLayerRemoved(int nLayerId)
{
    SY_INFOF("[LayerPersistenceBridge] Layer removed: id=%d", nLayerId);
    if (m_layerRepository)
    {
        if (!m_layerRepository->remove(m_documentId, nLayerId))
        {
            SY_ERRORF("[LayerPersistenceBridge] Failed to remove layer %d from database: %s",
                nLayerId, m_layerRepository->lastError().c_str());
        }
    }
}

/// 图层属性变更时同步更新数据库
void LayerPersistenceBridge::onLayerChanged(int nLayerId)
{
    SY_DEBUGF("[LayerPersistenceBridge] Layer changed: id=%d", nLayerId);
    syncLayerToDb(nLayerId);
}

/// 当前图层切换时同步（将当前图层标记为最新）
void LayerPersistenceBridge::onCurrentLayerChanged(int nLayerId)
{
    (void)nLayerId;
}

/// 图层可见性变更时只写 visible 字段
void LayerPersistenceBridge::onLayerVisibilityChanged(int nLayerId, bool bVisible)
{
    SY_DEBUGF("[LayerPersistenceBridge] Layer visibility changed: id=%d, visible=%d", nLayerId, bVisible);
    if (m_layerRepository)
    {
        if (!m_layerRepository->updateVisibility(m_documentId, nLayerId, bVisible))
        {
            SY_ERRORF("[LayerPersistenceBridge] Failed to update visibility for layer %d: %s",
                nLayerId, m_layerRepository->lastError().c_str());
        }
    }
}

/// 图层顺序变更时批量写入所有图层顺序
void LayerPersistenceBridge::onLayerOrderChanged()
{
    SY_INFO("[LayerPersistenceBridge] Layer order changed, batch updating all layers");
    if (!m_layerManager || !m_layerRepository)
        return;

    auto allLayers = m_layerManager->getAllLayerIds();
    std::vector<std::pair<int, int>> layerIdAndOrders;
    layerIdAndOrders.reserve(allLayers.size());
    for (int i = 0; i < static_cast<int>(allLayers.size()); ++i)
        layerIdAndOrders.emplace_back(allLayers[i], i);

    if (!m_layerRepository->batchUpdateOrder(m_documentId, layerIdAndOrders))
    {
        SY_ERRORF("[LayerPersistenceBridge] Failed to batch update layer order: %s",
            m_layerRepository->lastError().c_str());
    }
}

/// 将指定图层的当前状态写入数据库
void LayerPersistenceBridge::syncLayerToDb(int nLayerId)
{
    if (!m_layerManager || !m_layerRepository)
        return;

    // 从 LayerManager 读取图层当前状态
    std::string name = m_layerManager->layerName(nLayerId);
    Ut::Color layerClr = m_layerManager->layerColor(nLayerId);
    bool visible = m_layerManager->isLayerVisible(nLayerId);
    bool locked = m_layerManager->isLayerLocked(nLayerId);

    // 计算实际排序序号
    int orderIndex = nLayerId;
    auto allLayers = m_layerManager->getAllLayerIds();
    for (int i = 0; i < static_cast<int>(allLayers.size()); ++i)
    {
        if (allLayers[i] == nLayerId)
        {
            orderIndex = i;
            break;
        }
    }

    // 构造数据库记录
    LayerRecord record;
    record.documentId = m_documentId;
    record.layerId = nLayerId;
    record.name = name;
    record.color = layerClr.toHexRGB();
    record.visible = visible;
    record.locked = locked;
    record.orderIndex = orderIndex;
    record.updatedAt = QDateTime::currentDateTime().toString(Qt::ISODate).toStdString();

    // 写入数据库
    if (!m_layerRepository->save(record))
    {
        SY_ERRORF("[LayerPersistenceBridge] Failed to sync layer %d to database: %s",
            nLayerId, m_layerRepository->lastError().c_str());
    }
}