#pragma once

#include "UiCommandHandler.h"

namespace Eg
{
    struct SyEntity;
}
class SceneDocument2D;

/**
 * @file CommandSnapshots.h
 * @brief 命令快照辅助函数
 *
 * 提供实体几何快照的生成、恢复等辅助功能，支持 undo/redo 操作。
 */

 /**
  * 从实体生成几何快照
  * @param entity 实体指针
  * @return EntitySnapshot 快照结构体
  */
EntitySnapshot takeSnapshot(const Eg::SyEntity* entity);

/**
 * 从快照重建实体并使用 insertEntityPreserveId 恢复原始 ID
 * @param document 文档指针
 * @param snap 快照数据
 * @return 是否重建成功
 */
bool restoreFromSnapshot(SceneDocument2D* document, const EntitySnapshot& snap);

/**
 * 将快照几何数据覆盖到现有实体
 * @param entity 目标实体指针
 * @param snap 快照数据
 */
void restoreEntityGeometryFromSnapshot(Eg::SyEntity* entity, const EntitySnapshot& snap);