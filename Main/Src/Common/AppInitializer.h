#ifdef QT_CORE_LIB
#ifndef APPINITIALIZER_H
#define APPINITIALIZER_H

#include <QString>
#include <QSharedMemory>

class QApplication;

class AppInitializer
{
public:
    AppInitializer();
    ~AppInitializer();

public:
    bool initialize(int argc, char* argv[]);

    void setSingleInstance(bool enabled);
    bool isSingleInstance() const;

    void setApplicationName(const QString& name);
    QString getApplicationName() const;

    void setOrganizationName(const QString& name);
    QString getOrganizationName() const;

private:
    void setupOpenGLFormat();
    bool checkSingleInstance();

private:
    bool m_bSingleInstance;
    QString m_strAppName;
    QString m_strOrganizationName;
    QSharedMemory* m_pSharedMemory;
};

#endif // APPINITIALIZER_H
#endif
