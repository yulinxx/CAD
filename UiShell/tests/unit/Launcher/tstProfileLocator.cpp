#include <QtTest/QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include "Launcher/ProfileLocator.h"

namespace
{
    // 在 dir 下写一个最小 profile 文件，返回其绝对路径；失败返回空。
    QString writeProfile(const QString& dir, const QString& relativePath)
    {
        const QString path = QDir(dir).absoluteFilePath(relativePath);
        if (!QDir().mkpath(QFileInfo(path).absolutePath()))
            return QString();
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly))
            return QString();
        file.write("{ \"version\": 2 }\n");
        return QFileInfo(path).absoluteFilePath();
    }

    bool samePath(const QString& a, const QString& b)
    {
        return QFileInfo(a).canonicalFilePath() == QFileInfo(b).canonicalFilePath();
    }
}  // namespace

// ProfileLocator：<exe>/profile/ui.json → 内嵌资源 的优先级与报错。
class ProfileLocatorTest : public QObject
{
    Q_OBJECT

private slots:
    void init();

    void appDirBeatsEmbedded();
    void embeddedIsLastResort();
    void nothingFoundListsTriedPaths();
    void sourceNames();

private:
    QTemporaryDir m_tmp;
    QString m_appDir;
    QString m_deployed;
    QString m_embedded;
};

void ProfileLocatorTest::init()
{
    QVERIFY(m_tmp.isValid());
    m_appDir = QDir(m_tmp.path()).absoluteFilePath(QStringLiteral("bin"));
    QVERIFY(QDir().mkpath(m_appDir));
    m_deployed = writeProfile(m_appDir, QStringLiteral("profile/ui.json"));
    m_embedded = writeProfile(m_tmp.path(), QStringLiteral("embedded/ui.json"));
    QVERIFY(!m_deployed.isEmpty());
    QVERIFY(!m_embedded.isEmpty());
}

void ProfileLocatorTest::appDirBeatsEmbedded()
{
    const ProfileInfo loc = ProfileLocator::locate(m_appDir, m_embedded);
    QVERIFY2(loc.isValid(), qPrintable(loc.error));
    QCOMPARE(loc.source, ProfileSource::AppDir);
    QVERIFY(samePath(loc.path, m_deployed));
    QVERIFY(samePath(ProfileLocator::deployedPath(m_appDir), m_deployed));
}

void ProfileLocatorTest::embeddedIsLastResort()
{
    QVERIFY(QFile::remove(m_deployed));
    const ProfileInfo loc = ProfileLocator::locate(m_appDir, m_embedded);
    QVERIFY2(loc.isValid(), qPrintable(loc.error));
    QCOMPARE(loc.source, ProfileSource::Resource);
    QCOMPARE(loc.path, m_embedded);
    QCOMPARE(loc.tried.size(), 2);
}

void ProfileLocatorTest::nothingFoundListsTriedPaths()
{
    QVERIFY(QFile::remove(m_deployed));
    const QString missingEmbedded = QStringLiteral(":/NoSuchCustomer/profile/ui.json");
    const ProfileInfo loc = ProfileLocator::locate(m_appDir, missingEmbedded);
    QVERIFY(!loc.isValid());
    QCOMPARE(loc.source, ProfileSource::None);
    QCOMPARE(loc.tried.size(), 2);
    QVERIFY2(loc.error.contains(QStringLiteral("NoSuchCustomer")), qPrintable(loc.error));
}

void ProfileLocatorTest::sourceNames()
{
    QCOMPARE(ProfileLocator::sourceName(ProfileSource::AppDir), QStringLiteral("appDir"));
    QCOMPARE(ProfileLocator::sourceName(ProfileSource::Resource), QStringLiteral("embedded"));
    QCOMPARE(ProfileLocator::sourceName(ProfileSource::None), QStringLiteral("none"));
}

QTEST_GUILESS_MAIN(ProfileLocatorTest)

#include "tstProfileLocator.moc"
