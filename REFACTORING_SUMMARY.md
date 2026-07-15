# CAD 架构现状

## 当前架构

当前框架保留 **SyEntity 多态模型 + Protobuf 序列化** 方案，EntityRecord 相关的新架构尝试已移除。

---

## 核心数据模型

### SyEntity 多态体系

所有图元继承自 `SyEntity` 基类，支持多态操作：

- **简单图元**：SyLine、SyCircle、SyArc、SyPoint
- **复合图元**：SyPolygon、SySmartLine（包含多个子图元）
- **曲线**：SyBezier、SyBezier2、SyNurbs
- **特殊图元**：SyText、SyImage、SyQRCode、SyBarCode

### Protobuf 序列化

文件 I/O 使用 Protobuf 二进制格式，支持完整的实体类型体系：

- 支持所有 SyEntity 派生类型的序列化/反序列化
- 支持 SmartLine 等复合图元的嵌套序列化
- 支持图层、群组、文档元数据的完整存储

---

## 现有模块结构

| 模块 | 职责 |
|------|------|
| `Engine/Common` | SyEntity 基类、图层、通用实体接口 |
| `Engine/2D` | 2D 几何引擎、实体实现、编辑服务 |
| `Engine/3D` | 3D 几何引擎、网格、BRep |
| `FileIO` | 文件导入导出、Protobuf 序列化 |
| `UI/2D` | 2D 视图、工具系统、交互 |
| `UI/3D` | 3D 视图、场景树、相机控制 |
| `Render` | 渲染管线、Shader、OpenGL 后端 |

---

## 验证清单

- [ ] 所有单元测试通过
- [ ] DXF/DWG/SVG 导入导出正常
- [ ] 撤销/重做功能正常
- [ ] 图层锁定/可见性生效
- [ ] 选择/拖拽/编辑无崩溃
- [ ] 渲染帧率 ≥ 60fps (10k 实体)
- [ ] 内存占用无泄漏
- [ ] 文档保存/加载往返一致

---

## 后续优化方向

1. **命令系统统一**：完成 `OperationBus` 迁移，替代 `ICommandHandler`
2. **事务与 Undo 统一**：将 Undo/Redo 绑定到事务回放
3. **文档与编辑服务统一**：收敛文档事实源
4. **渲染路径收口**：清理历史 2D 渲染路径
5. **空壳模块清理**：移除 Network/Hardware/PythonHost 等空壳模块