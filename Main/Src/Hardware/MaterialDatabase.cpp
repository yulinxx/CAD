/**
 * @file MaterialDatabase.cpp
 * @brief 材料数据库实现：内置材料、JSON 持久化与查询
 */
#include "Hardware/MaterialDatabase.h"
#include "AppPathManager.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QStandardPaths>
#include <QDir>
#include "Log/SyLogger.h"

// 本文件实现刻意保留（ABI 兼容）的弃用类型 MaterialDatabase，定义自身成员时
// 必然要写出该类型名。调用方仍会收到弃用告警，因此这里只在本文件范围内压制。
#if defined(__GNUC__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

MaterialDatabase::MaterialDatabase(QObject* parent)
    : QObject(parent)
{
    // 先尝试加载数据库文件
    if (!loadFromFile())
    {
        loadBuiltinMaterials();
    }
}

MaterialDatabase::~MaterialDatabase()
{
    saveToFile();
}

bool MaterialDatabase::addMaterial(const MaterialInfo& material)
{
    QString key = makeKey(material.name, material.thickness);
    if (m_mMaterials.contains(key))
    {
        return false;
    }

    m_mMaterials[key] = material;
    emit sigMaterialAdded(material.name);
    emit sigDatabaseChanged();
    return true;
}

bool MaterialDatabase::removeMaterial(const QString& name, double thickness)
{
    QString key = makeKey(name, thickness);
    if (!m_mMaterials.contains(key))
    {
        return false;
    }

    m_mMaterials.remove(key);
    emit sigMaterialRemoved(name);
    emit sigDatabaseChanged();
    return true;
}

bool MaterialDatabase::updateMaterial(const QString& name, double thickness, const MaterialInfo& material)
{
    QString key = makeKey(name, thickness);
    if (!m_mMaterials.contains(key))
    {
        return false;
    }

    m_mMaterials[key] = material;
    emit sigDatabaseChanged();
    return true;
}

MaterialDatabase::MaterialInfo MaterialDatabase::getMaterial(const QString& name, double thickness) const
{
    QString key = makeKey(name, thickness);
    if (m_mMaterials.contains(key))
    {
        return m_mMaterials[key];
    }

    return MaterialInfo();
}

QList<MaterialDatabase::MaterialInfo> MaterialDatabase::getAllMaterials() const
{
    return m_mMaterials.values();
}

QList<MaterialDatabase::MaterialInfo> MaterialDatabase::getMaterialsByCategory(const QString& category) const
{
    QList<MaterialInfo> result;
    for (const auto& material : m_mMaterials)
    {
        if (material.category == category)
        {
            result.append(material);
        }
    }
    return result;
}

QStringList MaterialDatabase::getCategories() const
{
    QSet<QString> categories;
    for (const auto& material : m_mMaterials)
    {
        categories.insert(material.category);
    }
    return categories.values();
}

MaterialDatabase::MaterialInfo MaterialDatabase::getRecommendedSettings(const QString& name, double thickness) const
{
    return getMaterial(name, thickness);
}

bool MaterialDatabase::saveToFile(const QString& strFilePath)
{
    QString path = strFilePath.isEmpty() ? defaultDatabasePath() : strFilePath;

    QJsonObject root;
    QJsonArray materialsArray;

    for (auto it = m_mMaterials.begin(); it != m_mMaterials.end(); ++it)
    {
        QJsonObject obj;
        obj["name"] = it.value().name;
        obj["category"] = it.value().category;
        obj["thickness"] = it.value().thickness;
        obj["recommendPower"] = it.value().recommendPower;
        obj["recommendSpeed"] = it.value().recommendSpeed;
        obj["recommendFrequency"] = it.value().recommendFrequency;
        obj["recommendPasses"] = it.value().recommendPasses;
        materialsArray.append(obj);
    }

    root["materials"] = materialsArray;
    root["version"] = "1.0";

    QJsonDocument doc(root);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
    {
        SY_WARNF("[MaterialDatabase] Failed to save database: %s", path.toStdString().c_str());
        return false;
    }

    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

bool MaterialDatabase::loadFromFile(const QString& strFilePath)
{
    QString path = strFilePath.isEmpty() ? defaultDatabasePath() : strFilePath;

    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly))
    {
        return false;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isObject())
    {
        return false;
    }

    QJsonObject root = doc.object();
    QJsonArray materialsArray = root["materials"].toArray();

    m_mMaterials.clear();
    for (const auto& val : materialsArray)
    {
        QJsonObject obj = val.toObject();
        MaterialInfo info;
        info.name = obj["name"].toString();
        info.category = obj["category"].toString();
        info.thickness = obj["thickness"].toDouble(1.0);
        info.recommendPower = obj["recommendPower"].toDouble(50.0);
        info.recommendSpeed = obj["recommendSpeed"].toDouble(100.0);
        info.recommendFrequency = obj["recommendFrequency"].toDouble(20.0);
        info.recommendPasses = obj["recommendPasses"].toInt(1);

        m_mMaterials[makeKey(info.name, info.thickness)] = info;
    }

    SY_DEBUGF("[MaterialDatabase] Loaded %lld materials", static_cast<long long>(m_mMaterials.size()));
    return true;
}

bool MaterialDatabase::exportToJson(const QString& strFilePath) const
{
    return const_cast<MaterialDatabase*>(this)->saveToFile(strFilePath);
}

bool MaterialDatabase::importFromJson(const QString& strFilePath)
{
    return loadFromFile(strFilePath);
}

void MaterialDatabase::loadBuiltinMaterials()
{
    addAcrylicMaterials();
    addWoodMaterials();
    addMetalMaterials();
    addLeatherMaterials();
}

void MaterialDatabase::clear()
{
    m_mMaterials.clear();
    emit sigDatabaseChanged();
}

QString MaterialDatabase::makeKey(const QString& name, double thickness) const
{
    return QString("%1_%2").arg(name).arg(thickness, 0, 'f', 2);
}

QString MaterialDatabase::defaultDatabasePath() const
{
    return AppPathManager::materialDatabasePath();
}

void MaterialDatabase::addAcrylicMaterials()
{
    MaterialInfo info;
    info.category = CATEGORY_ACRYLIC;

    info.name = tr("Acrylic");
    info.thickness = 3.0;
    info.recommendPower = 60.0;
    info.recommendSpeed = 150.0;
    info.recommendFrequency = 20.0;
    info.recommendPasses = 1;
    addMaterial(info);

    info.thickness = 5.0;
    info.recommendPower = 70.0;
    info.recommendSpeed = 100.0;
    addMaterial(info);

    info.thickness = 8.0;
    info.recommendPower = 80.0;
    info.recommendSpeed = 60.0;
    info.recommendPasses = 2;
    addMaterial(info);
}

void MaterialDatabase::addWoodMaterials()
{
    MaterialInfo info;
    info.category = CATEGORY_WOOD;

    info.name = tr("Plywood");
    info.thickness = 3.0;
    info.recommendPower = 50.0;
    info.recommendSpeed = 200.0;
    info.recommendFrequency = 18.0;
    info.recommendPasses = 1;
    addMaterial(info);

    info.thickness = 6.0;
    info.recommendPower = 65.0;
    info.recommendSpeed = 120.0;
    info.recommendPasses = 2;
    addMaterial(info);

    info.name = tr("MDF");
    info.thickness = 3.0;
    info.recommendPower = 55.0;
    info.recommendSpeed = 180.0;
    info.recommendPasses = 1;
    addMaterial(info);
}

void MaterialDatabase::addMetalMaterials()
{
    MaterialInfo info;
    info.category = CATEGORY_METAL;

    info.name = tr("Stainless Steel");
    info.thickness = 0.5;
    info.recommendPower = 90.0;
    info.recommendSpeed = 30.0;
    info.recommendFrequency = 25.0;
    info.recommendPasses = 3;
    addMaterial(info);

    info.name = tr("Aluminum");
    info.thickness = 1.0;
    info.recommendPower = 85.0;
    info.recommendSpeed = 40.0;
    info.recommendFrequency = 22.0;
    info.recommendPasses = 2;
    addMaterial(info);
}

void MaterialDatabase::addLeatherMaterials()
{
    MaterialInfo info;
    info.category = CATEGORY_LEATHER;

    info.name = tr("Leather");
    info.thickness = 2.0;
    info.recommendPower = 30.0;
    info.recommendSpeed = 300.0;
    info.recommendFrequency = 15.0;
    info.recommendPasses = 1;
    addMaterial(info);
}

#if defined(__GNUC__)
#  pragma GCC diagnostic pop
#endif
