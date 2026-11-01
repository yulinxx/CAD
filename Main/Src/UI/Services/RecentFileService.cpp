/**
 * @file RecentFileService.cpp
 * @brief 最近文件服务实现
 *
 * 管理最近打开的文件列表，提供增删改查功能。
 * 2D / 3D 工作台的最近列表相互独立：写入按 workbenchId 分桶，读取按 workbenchId 过滤。
 */
#include "UI/Services/RecentFileService.h"

#include <QDateTime>
#include <QFileInfo>
#include <QSettings>

#include "Log/SyLogger.h"
#include "Persistence/PersistenceService.h"
#include "Persistence/Repositories/RecentFileRepository.h"
#include "Persistence/Models/RecentFileRecord.h"

namespace
{
    // 3D 工作台可打开的原生/模型格式；其余扩展名一律归 2D。
    // 仅用于没有工作台归属的旧记录（迁移前的 DB 行、legacy QSettings key）。
    const QStringList& k3DExtensions()
    {
        static const QStringList exts = { QStringLiteral("sy3d"), QStringLiteral("syx"), QStringLiteral("stl"),
            QStringLiteral("obj"), QStringLiteral("step"), QStringLiteral("stp") };
        return exts;
    }
}  // namespace

QString RecentFileService::normalizeWorkbench(const QString& workbenchId)
{
    const QString trimmed = workbenchId.trimmed().toUpper();
    return trimmed.isEmpty() ? QStringLiteral("2D") : trimmed;
}

QString RecentFileService::classifyWorkbenchByExtension(const QString& filePath)
{
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    return k3DExtensions().contains(suffix) ? QStringLiteral("3D") : QStringLiteral("2D");
}

RecentFileService::RecentFileService(PersistenceService* persistence)
    : m_persistence(persistence)
{
}

void RecentFileService::addRecentFile(const QString& filePath, const QString& workbenchId)
{
    if (filePath.isEmpty())
    {
        SY_DEBUGF("[RecentFileService] addRecentFile: empty path, ignored");
        return;
    }

    const QString wb = normalizeWorkbench(workbenchId);
    SY_DEBUGF("[RecentFileService] Adding recent file to %s: %s", qPrintable(wb), qPrintable(filePath));

    // 数据库端写入
    if (m_persistence && m_persistence->isOpen() && m_persistence->recentFiles())
    {
        QFileInfo fileInfo(filePath);
        RecentFileRecord rec;
        rec.filePath = filePath.toStdString();
        rec.title = fileInfo.fileName().toStdString();
        rec.format = fileInfo.suffix().toUpper().toStdString();
        rec.workbench = wb.toStdString();
        rec.lastOpenedTime = QDateTime::currentDateTime().toString(Qt::ISODate).toStdString();
        if (m_persistence->recentFiles()->append(rec))
        {
            SY_DEBUG("[RecentFileService] File added to database");
        }
        else
        {
            // 最近文件不是关键数据：数据库失败时 QSettings 兜底仍然写入，
            // 但不能像旧代码那样继续打印“已添加”。
            SY_WARNF("[RecentFileService] Failed to add recent file to database: %s (QSettings fallback still updated)",
                qPrintable(filePath));
        }
    }
    else
    {
        SY_DEBUG("[RecentFileService] Database not available, using QSettings fallback");
    }

    // QSettings 兜底双写（按工作台分 key）
    QStringList files = loadRecentFiles(wb);
    files.removeAll(filePath);
    files.prepend(filePath);
    while (files.size() > kMaxRecentFiles)
    {
        files.removeLast();
    }
    saveWorkbenchFiles(wb, files);
}

QStringList RecentFileService::loadRecentFiles(const QString& workbenchId) const
{
    const QString wb = normalizeWorkbench(workbenchId);

    // 数据库优先
    if (m_persistence && m_persistence->isOpen() && m_persistence->recentFiles())
    {
        auto records = m_persistence->recentFiles()->loadAll();
        if (!records.empty())
        {
            // SY_DEBUGF("[RecentFileService] Loaded %zu recent files from database", records.size());
            QStringList result;
            std::vector<RecentFileRecord> legacyFixups;
            result.reserve(static_cast<int>(records.size()));
            for (const auto& rec : records)
            {
                // 旧记录 workbench 为空（迁移前的 DB 行），按扩展名归类，避免 2D/3D 混显
                const bool legacyRow = rec.workbench.empty();
                const QString recWb = legacyRow
                    ? classifyWorkbenchByExtension(QString::fromStdString(rec.filePath))
                    : QString::fromStdString(rec.workbench);
                if (legacyRow)
                {
                    // 回写归类结果：workbench='' 的行不属于任何桶，
                    // append 的按桶裁剪永远裁不到它 —— 不修就会在库里无限累积
                    RecentFileRecord fixed = rec;
                    fixed.workbench = recWb.toStdString();
                    legacyFixups.push_back(std::move(fixed));
                }
                if (recWb == wb && result.size() < kMaxRecentFiles)
                {
                    result.append(QString::fromStdString(rec.filePath));
                }
            }
            for (const auto& fix : legacyFixups)
            {
                m_persistence->recentFiles()->append(fix);
            }

            if (!result.empty())
            {
                return result;
            }
            // DB 里有数据但没有**当前工作台**的条目：不能直接返回空列表 ——
            // 老版本写在 QSettings 里的 RecentFiles/<wb> 兜底数据会被整体吞掉。
            // 落到下方 QSettings 分支继续读。
        }
    }

    // QSettings 兜底：先读当前工作台的 key，为空再读 legacy key 按扩展名过滤
    SY_DEBUG("[RecentFileService] Database empty, falling back to QSettings");
    QSettings settings;
    QStringList files = settings.value(QStringLiteral("RecentFiles/%1").arg(wb), QStringList()).toStringList();
    if (files.isEmpty())
    {
        const QStringList legacy = settings.value(QStringLiteral("RecentFiles"), QStringList()).toStringList();
        for (const QString& path : legacy)
        {
            if (classifyWorkbenchByExtension(path) == wb)
            {
                files.append(path);
            }
        }
    }
    while (files.size() > kMaxRecentFiles)
    {
        files.removeLast();
    }
    return files;
}

void RecentFileService::saveWorkbenchFiles(const QString& workbenchId, const QStringList& files) const
{
    SY_DEBUGF("[RecentFileService] Saving %lld recent files to QSettings (%s)", files.size(), qPrintable(workbenchId));
    // 数据库端由 addRecentFile 逐条写入，此处不做批量覆盖
    // QSettings 兜底：按工作台分 key，互不覆盖
    QSettings settings;
    settings.setValue(QStringLiteral("RecentFiles/%1").arg(workbenchId), files);
}
