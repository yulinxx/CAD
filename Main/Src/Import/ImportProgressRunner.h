#pragma once

#include "Import/ImportContext.h"
#include "Import/ImportOptions.h"
#include "Import/ImportResult.h"

class ImportService;
class QWidget;

/// 以进度对话框驱动 ImportService 的异步导入。
///
/// 解析（Phase 1-2）在工作线程执行、落库（Phase 3-5）回到主线程，进度经线程安全的
/// ProgressTracker 汇总，由 ProgressDialog 每 100ms 刷新。对话框为窗口模态，
/// 导入完成（或失败/取消）后自动关闭。
///
/// run() 会进入局部事件循环直至导入结束，因此对调用方表现为同步返回，
/// 但期间主界面保持响应、进度可见，可用于拖放与菜单导入等场景。
class ImportProgressRunner
{
public:
    /// 执行一次带进度对话框的导入。
    /// @param service 导入服务（非拥有指针）
    /// @param context 导入上下文
    /// @param options 导入选项
    /// @param parent  进度对话框父窗口（可为空）
    /// @return 导入结果
    static ImportResult run(ImportService* service,
        const ImportContext& context,
        const ImportOptions& options,
        QWidget* parent = nullptr);
};
