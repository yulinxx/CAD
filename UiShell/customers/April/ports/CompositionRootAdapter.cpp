#include "CompositionRootAdapter.h"

// ---- AprilCore ----
#include "Composition/ApplicationCompositionRoot.h"

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
    IViewportFactory* CompositionRootAdapter::viewportFactory() { return &m_viewportFactory; }
    IViewportServices* CompositionRootAdapter::viewportServices() { return this; }
}
