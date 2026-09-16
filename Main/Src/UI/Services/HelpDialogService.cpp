#include "UI/Services/HelpDialogService.h"

#include "UI/Dlg/AboutDialog.h"
#include "UI/Shortcut/IShortcutSettingsModel.h"

#include <QInputDialog>
#include <QMessageBox>
#include <QWidget>

namespace
{
    /// 台账 → HTML 表格行：按分类分组，只列已绑定键位的命令。
    /// 台账是键位的唯一真值源（含用户覆盖），因此帮助对话框不必再维护手写键位表 ——
    /// 手写表必然随键位调整而失真，且 2D/3D 各写一份还会互相不一致。
    QString buildShortcutRows(const IShortcutSettingsModel& model)
    {
        QString rows;
        QString currentCategory;
        for (const IShortcutSettingsModel::Entry& entry : model.entries())
        {
            if (entry.currentKey.isEmpty())
            {
                continue;
            }
            const QString category = model.localizedCategory(entry.category);
            if (category != currentCategory)
            {
                currentCategory = category;
                rows += QStringLiteral("<tr><td colspan=\"2\"><b>%1</b></td></tr>")
                            .arg(currentCategory.toHtmlEscaped());
            }
            rows += QStringLiteral("<tr><td><b>%1</b></td><td>%2</td></tr>")
                        .arg(entry.currentKey.toString(QKeySequence::NativeText).toHtmlEscaped(),
                            model.localizedDisplayName(entry.displayName).toHtmlEscaped());
        }
        return rows;
    }
}  // namespace

void HelpDialogService::showAboutDialog(QWidget* parent)
{
    AboutDialog::showDialog(AppMode::Mode2D, parent);
}

void HelpDialogService::showAboutDialog(QWidget* parent, const QString& glVersion, const QString& glVendor,
    const QString& glRenderer, const QString& glslVersion)
{
    GLInfo glInfo;
    glInfo.version = glVersion;
    glInfo.vendor = glVendor;
    glInfo.renderer = glRenderer;
    glInfo.glsl = glslVersion;
    AboutDialog::showDialog(AppMode::Mode3D, glInfo, parent);
}

void HelpDialogService::showDocumentationDialog(QWidget* parent)
{
    QMessageBox::information(parent,
        QObject::tr("Documentation"),
        QObject::tr("Online documentation will be available soon.\n"
                    "Please visit https://docs.sanyicad.com for updates."));
}

void HelpDialogService::showShortcutsDialog(QWidget* parent, IShortcutSettingsModel* model)
{
    const QString rows = model ? buildShortcutRows(*model) : QString();

    QMessageBox msgBox(parent);
    msgBox.setWindowTitle(QObject::tr("Keyboard Shortcuts"));
    msgBox.setText(rows.isEmpty()
            ? QObject::tr("<h3>Keyboard Shortcuts</h3><p>No shortcuts are currently bound.</p>")
            : QObject::tr("<h3>Keyboard Shortcuts</h3><table>%1</table>").arg(rows));
    msgBox.setIcon(QMessageBox::Information);
    msgBox.exec();
}

// ==================== 通用弹窗 ====================

void HelpDialogService::showWarning(QWidget* parent, const QString& title, const QString& message)
{
    QMessageBox::warning(parent, title, message);
}

void HelpDialogService::showInformation(QWidget* parent, const QString& title, const QString& message)
{
    QMessageBox::information(parent, title, message);
}

int HelpDialogService::showQuestion(QWidget* parent, const QString& title, const QString& message)
{
    return QMessageBox::question(parent, title, message, QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
}

double HelpDialogService::getDouble(QWidget* parent,
    const QString& title,
    const QString& label,
    double value,
    double min,
    double max,
    int decimals,
    bool* ok)
{
    return QInputDialog::getDouble(parent, title, label, value, min, max, decimals, ok);
}