#include "CompilationStrategy.h"

// ============================================================================
// 增量编译判断
// ============================================================================

bool CompilationStrategy::canIncrementalCompile(const RenderContext& context) const
{
    return m_cacheValid && !context.isDirty && !m_forceFullCompile;
}

const std::set<std::string>& CompilationStrategy::dirtyEntityIds() const
{
    return m_dirtyEntityIds;
}

void CompilationStrategy::markEntityDirty(const std::string& entityId)
{
    m_dirtyEntityIds.insert(entityId);
}

void CompilationStrategy::markAllDirty()
{
    m_forceFullCompile = true;
    m_dirtyEntityIds.clear();
}

void CompilationStrategy::clearDirty()
{
    m_dirtyEntityIds.clear();
}

bool CompilationStrategy::hasDirtyEntities() const
{
    return !m_dirtyEntityIds.empty();
}

void CompilationStrategy::setForceFullCompile(bool force)
{
    m_forceFullCompile = force;
}

bool CompilationStrategy::forceFullCompile() const
{
    return m_forceFullCompile;
}

void CompilationStrategy::setCacheValid(bool valid)
{
    m_cacheValid = valid;
}

bool CompilationStrategy::cacheValid() const
{
    return m_cacheValid;
}