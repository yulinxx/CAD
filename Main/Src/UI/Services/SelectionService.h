#pragma once
/**
 * @file SelectionService.h
 * @brief 选择服务实现
 *
 * 将选择状态从 SceneDocument2D 中分离，委托给 Eg::SceneManager。
 * 实现 ISelectionService 窄接口，同时提供 Qt 便利方法供旧命令迁移使用。
 *
 * 使用方式：
 * @code
 *   SelectionService selService(sceneManager);
 *   selService.selectEntity("1");
 *   auto ids = selService.selectedIdsQ();
 * @endcode
 */
#include "ISelectionService.h"

#include <QPointF>
#include <QString>
#include <QVector>

namespace Eg
{
    class SceneManager;
}

/**
 * @class SelectionService
 * @brief 选择服务 —— 包装 Eg::SceneManager 的选择操作
 *
 * 职责：
 * - 实现 ISelectionService 窄接口（std::string）
 * - 提供 Qt 便利方法（QString），供旧命令层过渡使用
 * - 不持有文档数据，仅委托 SceneManager
 */
class SelectionService : public ISelectionService
{
public:
    explicit SelectionService(Eg::SceneManager* sceneManager);

    // ---- ISelectionService 窄接口 (std::string) ----

    std::vector<std::string> selectedIds() const override;
    bool isSelected(const std::string& id) const override;
    void select(const std::string& id) override;
    void selectMultiple(const std::vector<std::string>& ids) override;
    void deselect(const std::string& id) override;
    void clear() override;
    void toggle(const std::string& id) override;

    // ---- Qt 便利方法 (QString) ----

    /// 获取当前选中的图元 ID 列表（Qt 类型）
    QVector<QString> selectedIdsQ() const;

    /// 选中指定图元（不清除已有选择）
    void selectEntity(const QString& id);

    /// 设置唯一选中图元（清除已有选择）
    void setSelectedEntityId(const QString& id);

    /// 设置多个选中图元（清除已有选择）
    void setSelectedEntityIds(const QVector<QString>& ids);

    /// 点查询：返回点击位置最近的图元 ID
    QString entityIdAt(const QPointF& point, double tolerance = 5.0) const;

    /// 获取底层场景管理器
    Eg::SceneManager* sceneManager() const
    {
        return m_sceneManager;
    }

    std::vector<Eg::SyEntity*> selectedEntities() const override;

private:
    Eg::SceneManager* m_sceneManager;
};