#ifdef QT_CORE_LIB

#include "AppInitializer.h"
#include <QApplication>
#include <QDebug>
#include <QSurfaceFormat>
#include <QSharedMemory>
#include "GLVerDef.h"

AppInitializer::AppInitializer()
    : m_bSingleInstance(false)
    , m_strAppName("SanYi")
    , m_strOrganizationName("SanYi")
    , m_pSharedMemory(nullptr)
{
}

AppInitializer::~AppInitializer()
{
    if (m_pSharedMemory)
    {
        if (m_pSharedMemory->isAttached())
        {
            m_pSharedMemory->detach();
        }
        delete m_pSharedMemory;
    }
}

bool AppInitializer::initialize(int argc, char* argv[])
{
    setupOpenGLFormat();

    if (m_bSingleInstance && !checkSingleInstance())
    {
        return false;
    }

    QApplication::setApplicationName(m_strAppName);
    QApplication::setOrganizationName(m_strOrganizationName);

    return true;
}

void AppInitializer::setSingleInstance(bool enabled)
{
    m_bSingleInstance = enabled;
}

bool AppInitializer::isSingleInstance() const
{
    return m_bSingleInstance;
}

void AppInitializer::setApplicationName(const QString& name)
{
    m_strAppName = name;
}

QString AppInitializer::getApplicationName() const
{
    return m_strAppName;
}

void AppInitializer::setOrganizationName(const QString& name)
{
    m_strOrganizationName = name;
}

QString AppInitializer::getOrganizationName() const
{
    return m_strOrganizationName;
}

void AppInitializer::setupOpenGLFormat()
{
    QSurfaceFormat format;
    format.setVersion(TARGET_GL_VERSION_MAJOR, TARGET_GL_VERSION_MINOR);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    format.setSamples(4);
    QSurfaceFormat::setDefaultFormat(format);
}

bool AppInitializer::checkSingleInstance()
{
    QString sharedMemoryKey = QString("%1_Application_Instance").arg(m_strAppName);

    m_pSharedMemory = new QSharedMemory(sharedMemoryKey);

    if (m_pSharedMemory->attach())
    {
        qDebug() << "[AppInitializer] Another instance is already running";
        delete m_pSharedMemory;
        m_pSharedMemory = nullptr;
        return false;
    }

    if (!m_pSharedMemory->create(1, QSharedMemory::ReadWrite))
    {
        qWarning() << "[AppInitializer] Failed to create shared memory:"
            << m_pSharedMemory->errorString();
        delete m_pSharedMemory;
        m_pSharedMemory = nullptr;
        return false;
    }

    qDebug() << "[AppInitializer] Single instance initialized successfully";
    return true;
}

#endif