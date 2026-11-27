#include "MainWindow.h"

#include <QByteArray>
#include <QCloseEvent>
#include <QSettings>

namespace
{
    const char* const kGeometryKey = "geometry";
}  // namespace

MainWindow::MainWindow(const QString& settingsGroup, QWidget* parent)
    : QMainWindow(parent)
    , m_settingsGroup(settingsGroup)
{
}

MainWindow::~MainWindow() = default;

bool MainWindow::restoreWindowState()
{
    m_persistState = true;

    QSettings settings;
    settings.beginGroup(m_settingsGroup);
    const QByteArray geometry = settings.value(QLatin1String(kGeometryKey)).toByteArray();
    settings.endGroup();

    if (geometry.isEmpty())
        return false;

    return restoreGeometry(geometry);
}

void MainWindow::saveWindowState() const
{
    QSettings settings;
    settings.beginGroup(m_settingsGroup);
    settings.setValue(QLatin1String(kGeometryKey), saveGeometry());
    settings.endGroup();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (m_persistState)
        saveWindowState();

    QMainWindow::closeEvent(event);
}