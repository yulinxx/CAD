---
name: cad_architecture_cleanup
description: CAD软件架构清理项目 - dynamic_cast修复和deprecated类迁移
type: project
---

# CAD架构清理项目

## 当前工作
用户正在进行CAD软件架构清理，主要任务：
1. 修复跨DLL的dynamic_cast问题
2. 迁移deprecated的UI3D撤销命令类到Engine3D实现

## 关键技术决策

### dynamic_cast跨DLL问题
**问题**: 跨DLL使用dynamic_cast会导致静默失败，因为RTTI在多动态库进程中不可靠。

**修复方案**: 使用eType枚举 + static_cast替代
```cpp
// ❌ 错误 - 会静默失败
if (auto* line = dynamic_cast<const SyLine*>(entity))

// ✅ 正确
if (entity->eType == Eg::EType::LINE) {
    auto* line = static_cast<const SyLine*>(entity);
}
```

### 渐进式迁移
用户选择渐进式迁移方案，而非一次性大规模重写：
- 先添加新接口，同时保留旧接口
- 逐步替换调用点
- 验证后再删除deprecated类

## 进展状态

### 已完成
- UI3D UndoRedoManager3D: 添加了pushCommand(IUndoRedoCommand*)重载
- SceneEditService3D: 使用Engine3D命令替代makeDelete/makeAddMesh/makeTransform
- Geo2DSampling.cpp: 18处dynamic_cast已修复

### 进行中
- Geo2DAnalysis.cpp: 36处中修复了9处
- Geo2DEdit.cpp: 45处待处理

## 重要文件位置
- Engine/2D/Src/Algorithm/ - 包含Geo2DSampling.cpp, Geo2DAnalysis.cpp, Geo2DEdit.cpp
- EType定义: Engine/Common/Include/Engine/SyEntity/EType.h
- IUndoRedoCommand: Engine/Common/Include/Engine/Edit/IUndoRedoCommand.h
