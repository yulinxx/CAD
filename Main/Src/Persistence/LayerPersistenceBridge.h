#pragma once

#include "Engine2D/Interaction/ILayerManager.h"
#include "Persistence/Models/LayerRecord.h"

#include <string>

class LayerRepository;
class LayerManager;

/**
 * @brief 图层持久化桥接器 — 将 LayerManager 运行态变更同步写入 LayerRepository
 *
 * 实现 ILayerManagerObserver 接口，监听图层的新增、删除、属性变更等事件，
 * 并将变更实时写入数据库 LayerRepository，实现"运行态操作后同步落库"。
 *
 * 注意：此类只做单向同步（运行态 → 数据库），不做反向同步（数据库 → 运行态）。
 * 反向同步（文档加载时从数据库恢复图层列表）由其他机制负责。
 */
class LayerPersistenceBridge : public ILayerManagerObserver
{
public:
    /// @param layerManager 图层运行态管理器
    /// @param layerRepository 图层持久化仓储
    LayerPersistenceBridge(LayerManager* layerManager, LayerRepository* layerRepository);

    ~LayerPersistenceBridge() override = default;

    /// 开始监听图层变更事件
    void attach();

    /// 停止监听图层变更事件
    void detach();

    /// 设置当前文档 ID（单文档模式下为空字符串）
    void setDocumentId(const std::string& documentId);
    const std::string& documentId() const;

    // ---- ILayerManagerObserver 接口实现 ----

    void onLayerAdded(int nLayerId, const std::string& name) override;
    void onLayerRemoved(int nLayerId) override;
    void onLayerChanged(int nLayerId) override;
    void onCurrentLayerChanged(int nLayerId) override;
    void onLayerVisibilityChanged(int nLayerId, bool bVisible) override;
    void onLayerOrderChanged() override;

private:
    /// 将指定图层的当前状态写入数据库
    void syncLayerToDb(int nLayerId);

    LayerManager* m_layerManager{ nullptr };
    LayerRepository* m_layerRepository{ nullptr };
    bool m_attached{ false };
    std::string m_documentId;
};
