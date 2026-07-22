#pragma once

/// 导出选项：控制导出行为
struct ExportOptions
{
    /// 是否允许覆盖已存在的文件
    bool overwrite{ false };
    /// 导出完成后自动打开文件
    bool autoOpenAfterExport{ false };
    /// 是否嵌入字体（PDF/SVG 适用）
    bool embedFonts{ true };
    /// 是否将文本栅格化（转为路径）
    bool rasterizeText{ false };
    /// 是否启用输出压缩
    bool compressOutput{ false };
};
