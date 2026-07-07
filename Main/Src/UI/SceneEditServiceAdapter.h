/**
 * @file SceneEditServiceAdapter.h
 * @brief SceneEditService 适配器 — 让 EntityDocument2D 能被 OperationBus 使用
 *
 * 这是一个轻量级适配器，将 EntityDocument2D 的接口转换为 SceneEditService 的接口，
 * 使得 OperationBus 中的操作可以在旧系统中执行。
 *
 * 过渡期设计：
 * - 仅提供 OperationBus 所需的最小接口
 * - 将操作调用转发到 EntityDocument2D
 * - 后续随 EntityDocument2D 被替换而删除
 */
#pragma once

#include <QObject>
#include <QString>
#include <QPointF>
#include <QMap>
#include <vector>
#include <functional>

#include "UI/TransformParameters.h"

class EntityDocument2D;
class UiEntity;

namespace Eg
{
    using EntityId = uint64_t;
}

/**
 * @brief SceneEditService 适配器 — 让 EntityDocument2D 能被 OperationBus 使用
 *
 * 过渡期设计：
 * - 仅提供 OperationBus 所需的最小接口
 * - 将操作调用转发到 EntityDocument2D
 * - 后续随 EntityDocument2D 被替换而删除
 */
class SceneEditServiceAdapter : public QObject
{
    Q_OBJECT

public:
    explicit SceneEditServiceAdapter(EntityDocument2D* document, QObject* parent = nullptr);
    ~SceneEditServiceAdapter() override = default;

    /**
     * @brief 变换实体
     * @param ids 要变换的实体 ID 列表
     * @param transform 变换函数
     * @param description 变换描述
     * @param group 是否分组
     */
    void transformEntities(const std::vector<Eg::EntityId>& ids,
        std::function<void()> transform,
        const std::string& description,
        bool group);

    /**
     * @brief 获取当前选择的实体 ID 列表
     */
    std::vector<Eg::EntityId> getSelectedEntityIds() const;

    /**
     * @brief 获取所有实体 ID 列表
     */
    std::vector<Eg::EntityId> getAllEntityIds() const;

    /**
     * @brief 通知场景已变更
     */
    void notifySceneChanged();

    /**
     * @brief 开始变换（保存原始状态）
     */
    void beginTransform();

    /**
     * @brief 预览变换
     * @param params 变换参数
     */
    void previewTransform(const TransformParameters& params);

    /**
     * @brief 提交变换
     * @param params 变换参数
     * @return 是否成功
     */
    bool commitTransform(const TransformParameters& params);

    /**
     * @brief 取消变换（恢复原始状态）
     */
    void cancelTransform();

    /**
     * @brief 获取底层文档
     */
    EntityDocument2D* document() const { return m_document; }

signals:
    /// 变换预览信号
    void transformPreviewed(const TransformParameters& params);
    /// 变换提交信号
    void transformCommitted(const TransformParameters& params);
    /// 变换取消信号
    void transformCancelled();

private:
    /**
     * @brief 保存选择实体的原始位置
     */
    void saveOriginalPositions();

    /**
     * @brief 恢复原始位置
     */
    void restoreOriginalPositions();

    /**
     * @brief 清除原始位置缓存
     */
    void clearOriginalPositions();

    /**
     * @brief 应用变换到实体
     */
    void applyTransformToEntity(std::shared_ptr<UiEntity> entity, const TransformParameters& params);

private:
    EntityDocument2D* m_document{ nullptr };
    TransformParameters m_previewParams;
    bool m_previewActive{ false };

    /// 原始位置缓存：entityId -> originalCenter
    QMap<QString, QPointF> m_originalPositions;
};
