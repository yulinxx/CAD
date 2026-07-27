#pragma once

/// 导入选项：控制导入完成后的自动行为
struct ImportOptions
{
    /// 导入完成后自动适配视口到全图
    bool autoFit{ true };
    /// 导入完成后自动居中内容
    bool autoCenter{ true };
    /// 导入完成后自动切换工作台（2D/3D）
    bool autoSwitchWorkbench{ true };
    /// 作为新文档导入（清空当前场景）
    bool importAsNewDocument{ true };
    /// 合并到当前文档（不清空场景，追加图元）
    bool mergeIntoCurrentDocument{ false };
};
