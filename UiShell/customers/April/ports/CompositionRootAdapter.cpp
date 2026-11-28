#include "CompositionRootAdapter.h"

// ---- AprilCore ----
#include "Composition/ApplicationCompositionRoot.h"

#include <vector>

// ---- 属性面板算法产物：只在 adapter 内部引用，面板不见引擎/撤销细节 ----
#include "UI/Dlg/IPropertyEditTarget.h"
#include "UI2D/Service/EntityPropertyEditSession2D.h"

namespace
{
    std::vector<Eg::EntityId> toEntityIds(const QStringList& ids)
    {
        std::vector<Eg::EntityId> result;
        result.reserve(ids.size());
        for (const QString& id : ids)
        {
            bool ok = false;
            const qlonglong value = id.toLongLong(&ok);
            if (ok)
                result.push_back(static_cast<Eg::EntityId>(value));
        }
        return result;
    }
}  // namespace

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

    bool CompositionRootAdapter::hasOperationBus() const
    {
        return m_root && m_root->isBuilt() && m_root->operationBus() != nullptr;
    }

    bool CompositionRootAdapter::hasUiServices() const
    {
        return m_root && m_root->isBuilt();
    }

    PropertyModel CompositionRootAdapter::buildPropertyModel(const QStringList& ids) const
    {
        // 会话每次按 id 重新解析图元；buildModel 不修改场景，也不需要编辑权限。
        SceneEditService* sceneEdit = (m_root && m_root->isBuilt()) ? m_root->sceneEditService() : nullptr;
        const EntityPropertyEditSession2D session(sceneEdit, toEntityIds(ids));
        return session.buildModel();
    }

    std::shared_ptr<IPropertyEditTarget> CompositionRootAdapter::createEditTarget(const QStringList& ids)
    {
        if (!m_root || !m_root->isBuilt())
            return {};
        return std::make_shared<EntityPropertyEditSession2D>(m_root->sceneEditService(), toEntityIds(ids));
    }
}  // namespace Apr
