#include "UI/Services/HelpDialogService.h"

#include <QInputDialog>
#include <QMessageBox>
#include <QWidget>

void HelpDialogService::showAboutDialog(QWidget* parent)
{
    QMessageBox msgBox(parent);
    msgBox.setWindowTitle(QObject::tr("About SanYi CAD"));
    msgBox.setText(QObject::tr("<h3>SanYi CAD</h3>"
                               "<p>Version 1.0.0</p>"
                               "<p>Build: %1 %2</p>"
                               "<p>Qt %3 (built with %4)</p>"
                               "<p>C++17 &middot; MSVC</p>"
                               "<br/>"
                               "<p>Copyright &copy; 2026 SanYi Technology. All rights reserved.</p>")
            .arg(QString::fromLatin1(__DATE__),
                QString::fromLatin1(__TIME__),
                QString::fromLatin1(qVersion()),
                QString::fromLatin1(QT_VERSION_STR)));
    msgBox.setIcon(QMessageBox::Information);
    msgBox.exec();
}

void HelpDialogService::showSettingsDialog(QWidget* parent)
{
    QMessageBox::information(parent,
        QObject::tr("Settings"),
        QObject::tr("Settings dialog will be available in a future update.\n\n"
                    "Currently you can configure the following via the menu:\n"
                    "  - View > Show Grid / Snap Enabled / Ortho Mode / Angle Snap\n"
                    "  - Help > Language / Theme"));
}

void HelpDialogService::showDocumentationDialog(QWidget* parent)
{
    QMessageBox::information(parent,
        QObject::tr("Documentation"),
        QObject::tr("Online documentation will be available soon.\n"
                    "Please visit https://docs.sanyicad.com for updates."));
}

void HelpDialogService::showShortcutsDialog(QWidget* parent)
{
    QMessageBox msgBox(parent);
    msgBox.setWindowTitle(QObject::tr("Keyboard Shortcuts"));
    msgBox.setText(QObject::tr("<h3>Keyboard Shortcuts</h3>"
                               "<table>"
                               "<tr><td><b>Ctrl+N</b></td><td>New</td></tr>"
                               "<tr><td><b>Ctrl+O</b></td><td>Open</td></tr>"
                               "<tr><td><b>Ctrl+S</b></td><td>Save</td></tr>"
                               "<tr><td><b>Ctrl+Shift+S</b></td><td>Save As</td></tr>"
                               "<tr><td><b>Ctrl+Z</b></td><td>Undo</td></tr>"
                               "<tr><td><b>Ctrl+Y</b></td><td>Redo</td></tr>"
                               "<tr><td><b>Ctrl+A</b></td><td>Select All</td></tr>"
                               "<tr><td><b>Delete</b></td><td>Delete</td></tr>"
                               "<tr><td><b>Ctrl+C</b></td><td>Copy</td></tr>"
                               "<tr><td><b>Ctrl+V</b></td><td>Paste</td></tr>"
                               "<tr><td><b>Ctrl+X</b></td><td>Cut</td></tr>"
                               "<tr><td><b>Ctrl+F</b></td><td>Zoom to Fit</td></tr>"
                               "<tr><td><b>Ctrl++</b></td><td>Zoom In</td></tr>"
                               "<tr><td><b>Ctrl+-</b></td><td>Zoom Out</td></tr>"
                               "<tr><td><b>F1</b></td><td>Keyboard Shortcuts</td></tr>"
                               "</table>"));
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