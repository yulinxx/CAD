/**
 * @file DocumentRecoveryController.cpp
 * @brief 工作台级自动保存 / 崩溃恢复协调器实现
 */
#include "DocumentRecoveryController.h"

#include "Log/AprLogger.h"

#include <QDateTime>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTimer>
#include <QWidget>

#include <vector>

namespace
{
    constexpr const char* kEnabledKey = "dataSafety/autoSaveEnabled";
    constexpr const char* kIntervalKey = "dataSafety/autoSaveIntervalSeconds";
    constexpr int kDefaultIntervalSeconds = 60;
    // 轻量轮询：真正的触发节奏由 AutoSaveScheduler 控制，这里只负责喂时钟
    constexpr int kPollIntervalMilliseconds = 5000;

    QString defaultRecoveryDirectory()
    {
        char buffer[1024] = { 0 };
        if (!Fio::RecoveryStore::defaultRecoveryDirectory(buffer, sizeof(buffer)))
        {
            return {};
        }
        return QString::fromUtf8(buffer);
    }

    /// 收集残留恢复文件路径（for 循环回调不能用捕获 lambda）
    struct RecoveryCollector
    {
        std::vector<QString> paths;

        static void visit(const char* path, void* ctx)
        {
            if (ctx != nullptr && path != nullptr)
            {
                static_cast<RecoveryCollector*>(ctx)->paths.emplace_back(QString::fromUtf8(path));
            }
        }
    };
}  // namespace

DocumentRecoveryController::DocumentRecoveryController(
    const QString& workbenchId, bool is3D, QObject* parent)
    : QObject(parent)
    , m_workbenchId(workbenchId)
    , m_is3D(is3D)
    , m_store(new Fio::RecoveryStore())
    , m_scheduler(new Fio::AutoSaveScheduler())
{
    const QString recoveryDir = defaultRecoveryDirectory();
    if (!recoveryDir.isEmpty())
    {
        m_store->setRecoveryDirectory(recoveryDir.toUtf8().constData());
    }

    QSettings settings;
    const bool enabled = settings.value(QString::fromLatin1(kEnabledKey), true).toBool();
    const int interval = settings.value(QString::fromLatin1(kIntervalKey), kDefaultIntervalSeconds).toInt();
    m_scheduler->setEnabled(enabled);
    m_scheduler->setIntervalSeconds(interval);

    m_timer = new QTimer(this);
    m_timer->setInterval(kPollIntervalMilliseconds);
    connect(m_timer, &QTimer::timeout, this, &DocumentRecoveryController::onTick);
}

DocumentRecoveryController::~DocumentRecoveryController() = default;

void DocumentRecoveryController::setDialogParent(QWidget* parent)
{
    m_dialogParent = parent;
}

void DocumentRecoveryController::setDocumentKeyProvider(std::function<QString()> provider)
{
    m_documentKeyProvider = std::move(provider);
}

void DocumentRecoveryController::setDirtyProbe(std::function<bool()> probe)
{
    m_dirtyProbe = std::move(probe);
}

void DocumentRecoveryController::setSnapshotWriter(
    std::function<bool(const QString& recoveryPath, QString& error)> writer)
{
    m_snapshotWriter = std::move(writer);
}

void DocumentRecoveryController::setRestoreHandler(RestoreHandler handler)
{
    m_restoreHandler = std::move(handler);
}

bool DocumentRecoveryController::autoSaveEnabled() const
{
    return m_scheduler->enabled();
}

int DocumentRecoveryController::autoSaveIntervalSeconds() const
{
    return m_scheduler->intervalSeconds();
}

void DocumentRecoveryController::start()
{
    if (m_started)
    {
        return;
    }
    m_started = true;
    if (!m_clock.isValid())
    {
        m_clock.start();
    }
    m_scheduler->reset(m_clock.elapsed());

    // 先做崩溃恢复检查，再开始周期落盘：避免本轮 tick 把上会话残留的
    // 恢复文件当成「已保存后的垃圾」清掉。延后到事件循环启动后执行，
    // 保证提示框出现在主窗口就绪之后。
    if (m_scheduler->enabled())
    {
        m_timer->start();
    }
    QTimer::singleShot(0, this, [this]() { checkResidualRecovery(); });
}

void DocumentRecoveryController::stop()
{
    m_started = false;
    if (m_timer != nullptr)
    {
        m_timer->stop();
    }
}

QString DocumentRecoveryController::currentDocumentKey() const
{
    if (!m_documentKeyProvider)
    {
        return {};
    }
    const QString path = m_documentKeyProvider();
    if (!path.isEmpty())
    {
        return path;
    }
    // 未命名文档：同一次会话内保持稳定 key，保证多次自动保存写同一个恢复文件
    return QStringLiteral("untitled://") + m_workbenchId;
}

void DocumentRecoveryController::discardRecoveryFile(const QString& recoveryPath)
{
    if (recoveryPath.isEmpty())
    {
        return;
    }
    if (!m_store->discardRecovery(recoveryPath.toUtf8().constData()))
    {
        APR_WARNF("[DocumentRecovery] Failed to remove recovery file: %s", recoveryPath.toUtf8().constData());
    }
}

void DocumentRecoveryController::onTick()
{
    const long long now = m_clock.isValid() ? m_clock.elapsed() : 0;
    const bool dirty = m_dirtyProbe ? m_dirtyProbe() : false;

    if (!dirty)
    {
        // 手动保存成功（或撤销回干净状态）后清理本会话写出的恢复文件；
        // 只清理「本次会话确实写过」的，避免误删上会话的崩溃残留。
        if (m_wroteRecovery)
        {
            discardRecoveryFile(m_lastRecoveryPath);
            m_wroteRecovery = false;
            m_lastRecoveryPath.clear();
            m_lastWrittenKey.clear();
        }
        m_scheduler->tick(now, false);
        return;
    }

    if (!m_snapshotWriter)
    {
        return;
    }

    const QString key = currentDocumentKey();
    if (key.isEmpty())
    {
        return;
    }

    if (!m_scheduler->tick(now, true))
    {
        return;
    }

    // 文档换名（另存为）后，旧恢复文件不再对应任何文档，先清掉
    if (m_wroteRecovery && !m_lastWrittenKey.isEmpty() && m_lastWrittenKey != key)
    {
        discardRecoveryFile(m_lastRecoveryPath);
        m_wroteRecovery = false;
        m_lastRecoveryPath.clear();
    }

    char pathBuffer[1200] = { 0 };
    if (!m_store->prepareRecoveryPath(key.toUtf8().constData(), m_is3D, pathBuffer, sizeof(pathBuffer)))
    {
        APR_WARNF("[DocumentRecovery] Cannot resolve recovery path for: %s", key.toUtf8().constData());
        return;
    }

    const QString recoveryPath = QString::fromUtf8(pathBuffer);
    QString error;
    if (!m_snapshotWriter(recoveryPath, error))
    {
        // 失败不标记为已写：下一轮继续尝试，用户仍可能靠手动保存兜底
        APR_WARNF("[DocumentRecovery] Auto save failed for %s: %s",
            key.toUtf8().constData(),
            error.toUtf8().constData());
        return;
    }

    const Fio::SerializeResult manifestResult =
        m_store->writeRecoveryManifest(key.toUtf8().constData(), m_is3D);
    if (!manifestResult.success)
    {
        APR_WARNF("[DocumentRecovery] Recovery manifest failed for %s: %s",
            key.toUtf8().constData(),
            manifestResult.errorMessage);
    }

    m_lastWrittenKey = key;
    m_lastRecoveryPath = recoveryPath;
    m_wroteRecovery = true;
    APR_DEBUGF("[DocumentRecovery] Auto saved recovery for %s -> %s",
        key.toUtf8().constData(),
        recoveryPath.toUtf8().constData());
}

QString DocumentRecoveryController::displayNameFor(const QString& originalPath) const
{
    if (originalPath.isEmpty() || originalPath.startsWith(QStringLiteral("untitled://")))
    {
        return tr("Untitled document");
    }
    return originalPath;
}

void DocumentRecoveryController::checkResidualRecovery()
{
    QString directory;
    char dirBuffer[1024] = { 0 };
    const size_t dirLength = m_store->recoveryDirectory(dirBuffer, sizeof(dirBuffer));
    if (dirLength == 0)
    {
        directory = defaultRecoveryDirectory();
    }
    else
    {
        directory = QString::fromUtf8(dirBuffer);
    }
    if (directory.isEmpty())
    {
        return;
    }

    RecoveryCollector collector;
    m_store->forEachRecovery(directory.toUtf8().constData(), &RecoveryCollector::visit, &collector);
    for (const QString& path : collector.paths)
    {
        Fio::RecoveryFileInfo info;
        const Fio::SerializeResult inspectResult = m_store->inspectRecovery(path.toUtf8().constData(), info);
        // 不同工作台（2D/3D）的恢复文件交给对应工作台处理
        if (info.exists && info.is3D != m_is3D && inspectResult.success)
        {
            continue;
        }
        if (info.exists)
        {
            handleResidualRecovery(path, info);
        }
    }
}

void DocumentRecoveryController::handleResidualRecovery(
    const QString& recoveryPath, const Fio::RecoveryFileInfo& info)
{
    const QString displayName = displayNameFor(QString::fromUtf8(info.originalPath));

    if (!info.intact)
    {
        // 损坏的恢复文件必须如实告知，不能静默忽略
        QMessageBox box(m_dialogParent);
        box.setIcon(QMessageBox::Warning);
        box.setWindowTitle(tr("Damaged Recovery File"));
        box.setText(tr("A recovery file for \"%1\" is damaged and cannot be restored.").arg(displayName));
        box.setInformativeText(tr("Reason: %1\n\nFile: %2")
                .arg(QString::fromUtf8(info.errorMessage), recoveryPath));
        QPushButton* deleteButton = box.addButton(tr("Delete"), QMessageBox::DestructiveRole);
        box.addButton(tr("Keep"), QMessageBox::RejectRole);
        box.exec();
        if (box.clickedButton() == deleteButton)
        {
            discardRecoveryFile(recoveryPath);
        }
        return;
    }

    QString savedAtText;
    bool ok = false;
    const qlonglong epoch = QString::fromUtf8(info.savedAt).toLongLong(&ok);
    if (ok && epoch > 0)
    {
        savedAtText = QDateTime::fromSecsSinceEpoch(epoch).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    }

    QMessageBox box(m_dialogParent);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(tr("Recover Unsaved Work"));
    box.setText(tr("An autosaved recovery file was found for \"%1\".").arg(displayName));
    box.setInformativeText(savedAtText.isEmpty()
            ? tr("Restore this autosaved content? The original document on disk will not be modified.")
            : tr("Saved at: %1\n\nRestore this autosaved content? The original document on disk will not be "
                 "modified.")
                  .arg(savedAtText));
    QPushButton* restoreButton = box.addButton(tr("Restore"), QMessageBox::AcceptRole);
    QPushButton* discardButton = box.addButton(tr("Discard"), QMessageBox::DestructiveRole);
    box.exec();

    if (box.clickedButton() == discardButton)
    {
        discardRecoveryFile(recoveryPath);
        return;
    }
    if (box.clickedButton() != restoreButton)
    {
        return;
    }

    if (!m_restoreHandler)
    {
        QMessageBox::warning(m_dialogParent,
            tr("Restore Failed"),
            tr("This workbench cannot restore recovery files."));
        return;
    }

    QString error;
    if (!m_restoreHandler(recoveryPath, QString::fromUtf8(info.originalPath), error))
    {
        QMessageBox::warning(m_dialogParent,
            tr("Restore Failed"),
            tr("Failed to restore the autosaved content: %1\nThe recovery file was kept.").arg(error));
        return;
    }

    // 恢复成功：内容已进入内存文档，恢复文件使命结束
    discardRecoveryFile(recoveryPath);
    m_wroteRecovery = false;
    m_lastRecoveryPath.clear();
    m_lastWrittenKey.clear();
    if (m_clock.isValid())
    {
        m_scheduler->reset(m_clock.elapsed());
    }
}
