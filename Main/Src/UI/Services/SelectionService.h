#pragma once
/**
 * @file SelectionService.h
 * @brief 选择服务实现
 *
 * 将选择状态从 SceneDocument2D 中分离，委托给 Eg::SceneManager。
 * 实现 ISelectionService 窄接口（POD 安全），同时提供 Qt 便利方法供旧命令迁移使用。
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
#include <string>
#include <vector>

namespace Eg
{
    class SceneManager;
}

/**
 * @class SelectionService
 * @brief 选择服务 —— 包装 Eg::SceneManager 的选择操作
 *
 * 职责：
 * - 实现 ISelectionService 窄接口（POD 安全：const char* + 回调）
 * - 提供 Qt 便利方法（QString），供旧命令层过渡使用
 * - 不持有文档数据，仅委托 SceneManager
 */
class SelectionService : public ISelectionService
{
public:
    explicit SelectionService(Eg::SceneManager* sceneManager);

    SelectionService(const SelectionService&) = delete;
    SelectionService& operator=(const SelectionService&) = delete;

    // ---- ISelectionService 窄接口（POD 安全）----

    void visitSelectedIds(SelectedIdVisitor visitor, void* context) const override;
    bool isSelected(const char* id) const override;
    void select(const char* id) override;
    void selectMultiple(const char* const* ids, size_t count) override;
    void deselect(const char* id) override;
    void clear() override;
    void toggle(const char* id) override;

    // ---- Qt 便利方法 (QString) ----

    /// 获取当前选中的图元 ID 列表（Qt 类型，内部使用，非跨 DLL）
    QVector<QString> selectedIdsQ() const;

    /// 选中指定图元（不清除已有选择）
    void selectEntity(const QString& id);

    /// 设置唯一选中图元（清除已有选择）
    void setSelectedEntityId(const QString& id);

    /// 设置多个选中图元（清除已有选择）
    void setSelectedEntityIds(const QVector<QString>& ids);

    /// 点查询：返回点击位置最近的图元 ID
    QString entityIdAt(const QPointF& point, double tolerance = 5.0) const;

    /// 返回当前绑定的场景模型，供兼容测试和过渡代码使用。
    Eg::SceneManager* sceneManager() const
    {
        return m_sceneManager;
    }

    /// 绑定新的场景模型，供文档切换时更新委托目标
    void setSceneManager(Eg::SceneManager* sceneManager)
    {
        m_sceneManager = sceneManager;
    }

private:
    Eg::SceneManager* m_sceneManager;
};