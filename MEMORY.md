# CAD Architecture Cleanup Memory

This file tracks the ongoing architecture cleanup work for the SanYi CAD software.

## Active Work
- Fixing cross-DLL dynamic_cast issues (replacing with eType enum + static_cast)
- Migrating deprecated UI3D undo/redo commands to Engine3D implementations

## Key Technical Notes
- Eg::EType enum defined in Engine/Common/Include/Engine/SyEntity/EType.h
- IUndoRedoCommand interface in Engine/Common/Include/Engine/Edit/IUndoRedoCommand.h
- Use `entity->eType == Eg::EType::LINE` pattern instead of dynamic_cast

## Files Being Modified
- Engine/2D/Src/Algorithm/Geo2DSampling.cpp (complete)
- Engine/2D/Src/Algorithm/Geo2DAnalysis.cpp (in progress)
- Engine/2D/Src/Algorithm/Geo2DEdit.cpp (pending)
- UI/3D/Src/Edit/SceneEditService3D.cpp (complete)
- UI/3D/Include/UI3D/Edit/UndoRedoManager3D.h (modified)
