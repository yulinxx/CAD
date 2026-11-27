#include "CompositionRootAdapter.h"

// ---- AprilCore ----
#include "Composition/ApplicationCompositionRoot.h"
#include "UI/Services/UiShellHost.h"
#include "UI/Workbench/WorkbenchWindow.h"
#include "UI/Workbench/UiWorkbench.h"
#include <QVariantMap>

namespace Apr
{
    CompositionRootAdapter::CompositionRootAdapter(ApplicationCompositionRoot& root)
        : m_root(&root)
    {
    }

    bool CompositionRootAdapter::hasSceneDocument2D() const
    {
        return m_root && m_root->isBuilt() && m_root->document2D() != nullptr;
    }
    bool CompositionRootAdapter::hasSceneDocument3D() const
    {
        return m_root && m_root->isBuilt() && m_root->sceneManager3D() != nullptr;
    }
    bool CompositionRootAdapter::hasOperationBus() const
    {
        return m_root && m_root->isBuilt() && m_root->operationBus() != nullptr;
    }
    bool CompositionRootAdapter::hasSelectionService() const
    {
        return m_root && m_root->isBuilt() && m_root->selectionService() != nullptr;
    }
    bool CompositionRootAdapter::hasLayerManager() const
    {
        return m_root && m_root->isBuilt() && m_root->layerManager() != nullptr;
    }
    bool CompositionRootAdapter::hasUiStateCenter() const
    {
        return m_root && m_root->isBuilt() && m_root->stateCenter() != nullptr;
    }
    bool CompositionRootAdapter::hasCameraController3D() const
    {
        return m_root && m_root->isBuilt();
    }
    bool CompositionRootAdapter::hasUiServices() const
    {
        return m_root && m_root->isBuilt();
    }
}  // namespace Apr

namespace Apr
{
    bool CompositionRootAdapter::canUndo() const
    {
        return m_root && m_root->isBuilt() && m_root->undoRedoManager() && m_root->undoRedoManager()->canUndo();
    }
    bool CompositionRootAdapter::canRedo() const
    {
        return m_root && m_root->isBuilt() && m_root->undoRedoManager() && m_root->undoRedoManager()->canRedo();
    }
    void CompositionRootAdapter::undo()
    {
        if (m_root && m_root->isBuilt() && m_root->undoRedoManager())
            m_root->undoRedoManager()->undo();
    }
    void CompositionRootAdapter::redo()
    {
        if (m_root && m_root->isBuilt() && m_root->undoRedoManager())
            m_root->undoRedoManager()->redo();
    }
    bool CompositionRootAdapter::dispatchOperation(const QString& commandId)
    {
        if (!m_root || !m_root->isBuilt() || !m_root->shellHost() || !m_root->shellHost()->mainWindow()
            || !m_root->shellHost()->mainWindow()->currentWorkbench())
            return false;
        m_root->shellHost()->mainWindow()->currentWorkbench()->dispatch(commandId, QVariantMap{});
        return true;
    }

    IViewportFactory* CompositionRootAdapter::viewportFactory() { return &m_viewportFactory; }
    IViewportServices* CompositionRootAdapter::viewportServices() { return this; }
}
