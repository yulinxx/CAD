# 3D 撤销与 B-Rep 同步优化

## 版本
v1.0 - 2026-10-27

## 状态
已完成核心修复并通过编译验证：
- 默认构建（GMC OFF）：`UI3D` + `SanYiCAD` 编译通过
- GMC 构建（`build-gmc`，GeoModelCore ON）：`UI3D` + `SanYiCAD` 编译通过
- 启动冒烟测试：两版应用均可正常启动、UI shell 显示、空场景刷新完成

## 已修复问题 (按优先级)

### P0 - 撤销栈 UAF
- **Open/New 前清空撤销栈**：防止旧命令持有已释放实体裸指针
- **Import 追加改用 `ReplaceEntitiesCommand3D`**：保留旧实体指针，仅 `replaceScene=true` 时清 `modified`
- **`File_NewScene` 清栈**：新建场景清空撤销栈

### P1 - 隐藏/锁定语义
- **隐藏图元过滤**：`raycast/raycastFirst/raycastHitFirst/queryByPoint/queryByBox/selectAll/invertSelection` 统一跳过 `entity->visible() == false` 的图元
- **锁定图元拒绝变换/删除**：`beginTransform/applyTransform`、`keyPressEvent` nudge、`deleteEntity` 均检查锁定标志
- **场景树删除统一走 `deleteEntities`**：`deleteSelected` 复用该逻辑

### B-Rep - 变换同步
- **`TransformMeshCommand3D` 新增正/逆矩阵回调**：undo 应用逆矩阵，redo 应用正矩阵；奇异矩阵退化为仅还原网格
- **回调上下文由 `UndoRedoManager3D` 持有**：撤销栈跨工作台切换保留，而 `SceneEditService3D` 随切台销毁，避免 UAF
- **`SceneEditService3D` 析构时自动解绑**：`bindBRepService(nullptr)`，确保切台后 undo 安全
- **文档级替换清空 B-ROP 映射**：Open/New 时 `clearEntityMap()`，防止新旧文档实体 id 复用时错误命中

### P2 - 数据持久化与保存点
- **proto 新增 `visible=50` / `locked=51`**：可选字段，兼容旧文件（缺省 `visible=true`、`locked=false`）
- **MESH 强转 UB 修复**：序列化/反序列化时 `if (entity.eType != EType::MESH)` 分支，避免把 `SyMeshEntity` 强转 `SyEntity2D`
- **保存点逻辑修复**：`isAtSavePoint` 支持无效哨兵；合并/新分支/裁剪时置为无效；保存成功/New/Open 后 `markSavePoint`；`OperationBus3D::postExecute` 用 `!isAtSavePoint()` 决定脏标记
- **导入解析器/重入计数 RAII**：`ParserGuard`/`ImportDepthGuard`，解析/转换异常转为错误提示

### P3 - 崩溃与健壮性
- **`RenderWidget3D::keyPressEvent` 空指针保护**：首行 `if (!event) return;`；`if (!m_selectionManager)` 兜底
- **投影远平面改用可见包围盒**：`getVisibleSceneBBox()`，隐藏远程图元不拉大 far/near 比值
- **`Mesh3DBuilder` 分配/写入失败保留旧 block**：分配失败不删除条目，写入失败保留旧内容
- **包围盒缓存失效同步**：实体增/改/删/可见性/锁定变化时同步 `m_sceneBBoxValid = false; m_visibleSceneBBoxValid = false`；查询改用 `find` 避免 `operator[]` 插入空条目

## 已提交记录 (子模块，均已推送至 main 分支)

| 仓库 | 提交 | 简要说明 |
|------|------|----------|
| Engine | `6abb926` | 隐藏图元拾取过滤、可见包围盒、B-ROP 变换撤销回调 |
| FileIO | `5faf4fe` | 可见性/锁定持久化、MESH 序列化 UB 修复 |
| UI | `9b505d5` | 撤销生命周期/保存点、B-ROP 同步、导入异常安全等 |
| 父仓库 | `a68b8dfc` | 同步三个子模块指针 + `Workbench3D` 接线 |

## 已知限制 (Future Work)

1. **B-ROP 拓扑不持久化**：保存/重开后需重新从 STEP 等源文件挂接
2. **Open/New 撤销后 B-ROP 无法恢复**：基于整场景克隆 trade-off，接受映射在文档替换时被清理
3. **`make test` 受 Engine2D `TextConverter.h` 预置错误阻塞**，与本任务无关
4. **Windows/MSVC 与 Qt 6.5/6.11 的二进制兼容性**仍需单独验证

## 联系方式 / 维护
- 维护人员：3D 模块组
- 相关子模块：Engine (`3D/Include/Engine3D/`)、FileIO (`FileIO/Proto/SanYiDocument.proto`)、UI (`UI/3D/Src/`)
- 问题跟踪：见 Git Issues / 项目 Wiki