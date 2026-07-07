#pragma once

#include "RenderCoreApi.h"
#include "RenderTypes.h"
#include "RenderFrame.h"

#include <QList>
#include <QSet>

class EntityDocument2D;
class SceneDocument3D;
struct RenderContext;

namespace Eg { class SceneManager; }

/**
 * @file SceneTraverser.h
 * @brief 场景遍历器
 *
 * 负责遍历场景文档，收集实体信息并生成渲染批次。
 *
 * 职责边界：
 * - 遍历 2D 文档：线段、折线、圆、圆弧
 * - 遍历 3D 文档：节点、子节点
 * - 收集选中状态
 * - 生成 RenderBatch 列表
 *
 * 不承担：
 * - 增量编译决策（由 CompilationStrategy 负责）
 * - 缓存管理（由 BatchManager 负责）
 * - 批次分组与裁剪（由 BatchManager 负责）
 */
class RENDER_CORE_API SceneTraverser
{
public:
    /// 遍历 2D 文档生成批次（旧版 EntityDocument2D 路径）
    QList<RenderBatch> traverse2D(EntityDocument2D* document, const RenderContext& context);

    /// 遍历 2D 场景生成批次（新版 Eg::SceneManager 路径）
    QList<RenderBatch> traverse2D(Eg::SceneManager* scene, const RenderContext& context);

    /// 遍历 3D 文档生成批次
    QList<RenderBatch> traverse3D(SceneDocument3D* document, const RenderContext& context);

private:
    /// 从文档选择中提取选中 ID 集合
    static QSet<QString> extractSelectedIds(void* document);
};