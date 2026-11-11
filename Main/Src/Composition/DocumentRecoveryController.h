/**
 * @file DocumentRecoveryController.h
 * @brief 工作台级自动保存 / 崩溃恢复协调器
 *
 * 职责：
 *   - 定时推进 AutoSaveScheduler，仅在有未保存修改时写独立恢复文件
 *   - 手动保存成功（脏标记消失）后清理本会话写出的恢复文件
 *   - 启动时检测残留恢复文件，明确提示用户「恢复 / 丢弃」
 *
 * 2D 与 3D 共用本类，差异只在注入的快照写出与恢复回调，语义保持一致。
 */
#pragma once

#include "FileIO/DocumentRecovery.h"

#include <QElapsedTimer>
#include <QObject>

#include <functional>
#include <memory>

class QTimer;
class QWidget;

class DocumentRecoveryController : public QObject
{
    Q_OBJECT

public:
    /// 恢复回调：把恢复文件内容载入当前文档，原文档文件保持原样
    using RestoreHandler =
        std::function<bool(const QString& recoveryPath, const QString& originalPath, QString& error)>;

    explicit DocumentRecoveryController(const QString& workbenchId, bool is3D, QObject* parent = nullptr);
    ~DocumentRecoveryController() override;

    /// 提示框父窗口（首次显示前注入）
    void setDialogParent(QWidget* parent);

    /// 当前文档路径；未命名文档返回空字符串
    void setDocumentKeyProvider(std::function<QString()> provider);
    /// 当前文档是否有未保存修改
    void setDirtyProbe(std::function<bool()> probe);
    /// 把当前场景写成指定路径的恢复文件（失败必须填充 error）
    void setSnapshotWriter(std::function<bool(const QString& recoveryPath, QString& error)> writer);
    void setRestoreHandler(RestoreHandler handler);

    /// 启动定时器并检测上次会话残留的恢复文件
    void start();
    void stop();

    bool autoSaveEnabled() const;
    int autoSaveIntervalSeconds() const;

private:
    void onTick();
    void checkResidualRecovery();
    void handleResidualRecovery(const QString& recoveryPath, const Fio::RecoveryFileInfo& info);

    QString currentDocumentKey() const;
    QString displayNameFor(const QString& originalPath) const;
    void discardRecoveryFile(const QString& recoveryPath);

private:
    QString m_workbenchId;
    bool m_is3D{ false };
    QWidget* m_dialogParent{ nullptr };

    std::function<QString()> m_documentKeyProvider;
    std::function<bool()> m_dirtyProbe;
    std::function<bool(const QString&, QString&)> m_snapshotWriter;
    RestoreHandler m_restoreHandler;

    std::unique_ptr<Fio::RecoveryStore> m_store;
    std::unique_ptr<Fio::AutoSaveScheduler> m_scheduler;
    QTimer* m_timer{ nullptr };
    QElapsedTimer m_clock;

    QString m_lastWrittenKey;
    QString m_lastRecoveryPath;
    bool m_wroteRecovery{ false };
    bool m_started{ false };
};
