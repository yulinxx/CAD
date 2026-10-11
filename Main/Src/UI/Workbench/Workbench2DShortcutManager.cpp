#include "Workbench2DShortcutManager.h"

#include "RenderViewport2D.h"
#include "UI2D/Operation/OperationBus.h"
#include "UI2D/Operation/OperationId.h"
#include "UI/Services/ISelectionService.h"
#include "UI/Workbench/WorkbenchWindow.h"

#include <QApplication>
#include <QLineEdit>
#include <QTextEdit>
#include <QPlainTextEdit>
#include <QShortcut>

Workbench2DShortcutManager::Workbench2DShortcutManager(QObject* parent)
    : QObject(parent)
{
}

Workbench2DShortcutManager::~Workbench2DShortcutManager() = default;

void Workbench2DShortcutManager::registerGlobalShortcuts(WorkbenchWindow& window, RenderViewport2D* viewport,
    OperationBus* bus, ISelectionService* selectionService)
{
    const auto editingText = []() -> bool {
        QWidget* fw = QApplication::focusWidget();
        return fw && (qobject_cast<QLineEdit*>(fw) || qobject_cast<QTextEdit*>(fw) || qobject_cast<QPlainTextEdit*>(fw));
    };
    const auto deleteSelectedShapes = [this, viewport, bus, editingText](bool forward) {
        if (editingText())
        {
            return;
        }
        if (viewport && viewport->handleTextDeleteRequest(forward))
        {
            return;
        }
        if (viewport && viewport->handleStepBackRequest())
        {
            return;
        }
        if (bus)
        {
            SY_DEBUG("[Workbench2DShortcutManager] Delete shortcut activated, running Edit_Delete operation");
            bus->run(OperationId::Edit_Delete, {}, OperationSource::Shortcut);
        }
    };
    const auto clearSelectionShapes = [this, viewport, bus, selectionService, editingText]() {
        if (editingText())
        {
            return;
        }
        if (viewport && viewport->handleEscapeRequest())
        {
            return;
        }
        if (selectionService)
        {
            selectionService->clear();
        }
        if (viewport)
        {
            viewport->requestFullRefresh();
        }
    };

    auto* deleteSc = new QShortcut(QKeySequence(Qt::Key_Delete), &window);
    deleteSc->setContext(Qt::ApplicationShortcut);
    QObject::connect(deleteSc, &QShortcut::activated, this, [deleteSelectedShapes]() {
        deleteSelectedShapes(true);
    });
    window.registerShortcut(deleteSc);
    m_shortcuts.push_back(deleteSc);

    auto* backspaceSc = new QShortcut(QKeySequence(Qt::Key_Backspace), &window);
    backspaceSc->setContext(Qt::ApplicationShortcut);
    QObject::connect(backspaceSc, &QShortcut::activated, this, [deleteSelectedShapes]() {
        deleteSelectedShapes(false);
    });
    window.registerShortcut(backspaceSc);
    m_shortcuts.push_back(backspaceSc);

    auto* selectAllSc = new QShortcut(QKeySequence::SelectAll, &window);
    QObject::connect(selectAllSc, &QShortcut::activated, this, [bus, editingText]() {
        if (!editingText() && bus)
        {
            bus->run(OperationId::Edit_SelectAll, {}, OperationSource::Shortcut);
        }
    });
    window.registerShortcut(selectAllSc);
    m_shortcuts.push_back(selectAllSc);

    auto* escSc = new QShortcut(QKeySequence(Qt::Key_Escape), &window);
    QObject::connect(escSc, &QShortcut::activated, this, clearSelectionShapes);
    window.registerShortcut(escSc);
    m_shortcuts.push_back(escSc);

    auto* captureSc = new QShortcut(QKeySequence(Qt::Key_F12), &window);
    QObject::connect(captureSc, &QShortcut::activated, this, [bus, editingText]() {
        if (editingText() || !bus)
        {
            return;
        }
        bus->run(OperationId::View_Capture, {}, OperationSource::Shortcut);
    });
    window.registerShortcut(captureSc);
    m_shortcuts.push_back(captureSc);
}

void Workbench2DShortcutManager::unregisterGlobalShortcuts()
{
    for (auto* sc : m_shortcuts)
    {
        if (sc)
        {
            sc->setEnabled(false);
        }
    }
    m_shortcuts.clear();
}
