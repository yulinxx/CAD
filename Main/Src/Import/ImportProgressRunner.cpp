#include "Import/ImportProgressRunner.h"

#include <algorithm>
#include <memory>

#include <QPointer>
#include <QWidget>

#include "Import/ImportService.h"
#include "Log/SyLogger.h"
#include "UI/Dlg/ProgressDialog.h"
#include "UI/Progress/ProgressTracker.h"

namespace
{
    // 各阶段在总进度中的区间：与 ImportPhase 一一对应，区间之和为 1.0。
    // 阶段内部进度（0~1）线性映射到 [base, base + span]。
    float phaseBase(ImportPhase phase)
    {
        switch (phase)
        {
        case ImportPhase::DetectFormat:   return 0.00f;
        case ImportPhase::Parse:          return 0.05f;
        case ImportPhase::BuildDocument:  return 0.60f;
        case ImportPhase::RefreshDisplay: return 0.95f;
        case ImportPhase::WriteBackState: return 1.00f;
        default:                          return 0.00f;
        }
    }

    float phaseSpan(ImportPhase phase)
    {
        switch (phase)
        {
        case ImportPhase::DetectFormat:   return 0.05f;
        case ImportPhase::Parse:          return 0.55f;
        case ImportPhase::BuildDocument:  return 0.35f;
        case ImportPhase::RefreshDisplay: return 0.05f;
        case ImportPhase::WriteBackState: return 0.00f;
        default:                          return 0.00f;
        }
    }

    const char* phaseLabel(ImportPhase phase)
    {
        switch (phase)
        {
        case ImportPhase::DetectFormat:   return "识别文件格式";
        case ImportPhase::Parse:          return "解析文件";
        case ImportPhase::BuildDocument:  return "写入场景";
        case ImportPhase::RefreshDisplay: return "刷新显示";
        case ImportPhase::WriteBackState: return "收尾";
        default:                          return "导入中";
        }
    }
}  // namespace

ImportResult ImportProgressRunner::run(
    ImportService* service, const ImportContext& inContext, const ImportOptions& options, QWidget* parent)
{
    if (!service)
    {
        return ImportResult::fail(QStringLiteral("Import service unavailable"), ImportErrorType::Unknown);
    }

    auto tracker = std::make_shared<ProgressTracker>();
    tracker->addStage("导入文件", 1.0, 100.0);
    tracker->begin();
    tracker->beginStage(0, phaseLabel(ImportPhase::DetectFormat));

    auto* dialog = new ProgressDialog(tracker, QStringLiteral("导入文件"), parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setCancelable(true);

    // 对话框可能在导入结束前被强制关闭（连续两次取消），用 QPointer 兜底防止悬空
    QPointer<ProgressDialog> dialogPtr = dialog;
    auto result =
        std::make_shared<ImportResult>(ImportResult::fail(QStringLiteral("Import canceled"), ImportErrorType::Canceled));

    // 落库阶段（BuildDocument 起）不可取消：图元已在事务中，无法中途回滚。
    // 该阶段回调固定跑在主线程，故可安全触碰对话框。
    auto applyPhaseEntered = std::make_shared<bool>(false);

    // 进度/取消桥接到线程安全的 tracker：解析阶段在工作线程写、落库阶段在主线程写，
    // tracker 内部全程加锁，两侧都可安全调用。
    ImportContext context = inContext;
    context.progressCallback = [tracker, dialogPtr, applyPhaseEntered](ImportPhase phase, float progress) {
        const float clamped = std::clamp(progress, 0.0f, 1.0f);
        const float overall = phaseBase(phase) + phaseSpan(phase) * clamped;
        tracker->updateStage(static_cast<double>(overall) * 100.0, phaseLabel(phase));

        if (phase == ImportPhase::BuildDocument && !*applyPhaseEntered)
        {
            *applyPhaseEntered = true;
            if (dialogPtr)
            {
                dialogPtr->beginApplyPhase();
            }
        }
    };
    context.cancelCallback = [tracker]() { return tracker->isCancelRequested(); };

    dialog->startExternalTracking(true);

    service->importAsync(context, options, [dialogPtr, result, tracker, applyPhaseEntered](const ImportResult& r) {
        *result = r;
        // 不调用 tracker->end()：它会把 isRunning 置 false，导致 getElapsedMs() 归零，
        // 对话框收尾读到的耗时变成 0。这里只把进度推到 100%，计时由对话框自己停。
        tracker->updateStage(100.0, r.success ? "完成" : "已结束");
        if (dialogPtr)
        {
            if (*applyPhaseEntered)
            {
                dialogPtr->endApplyPhase();
            }
            dialogPtr->finishExternalTracking(r.success);
        }
    });

    dialog->show();
    dialog->exec();
    return *result;
}
