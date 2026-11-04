/**
 * @file MaterialDatabaseTests.cpp
 * @brief 材料数据库单元测试
 *
 * 测试目标：
 * - 验证材料添加、删除、更新
 * - 验证材料查询
 * - 验证内置材料加载
 */

#include <gtest/gtest.h>
#include <QStandardPaths>
#include <memory>
#include "Hardware/MaterialDatabase.h"

/**
 * @brief 材料库测试夹具。
 *
 * MaterialDatabase 的构造函数会从磁盘加载持久化 JSON、析构函数会写回，
 * 因此不做隔离时用例之间会通过 AppData 里那个文件互相污染
 * （表现为 addMaterial 返回 false —— 材料「已经存在」），
 * 失败的用例还会随执行顺序漂移；更糟的是跑一次测试就改了用户真实的材料库。
 *
 * 这里加两道隔离：
 *   1. QStandardPaths 测试模式：把可写目录重定向到 qttest 子目录，不碰真实数据；
 *   2. 每个用例开头 clear()：从确定的空库开始，而不是从内置材料 + 上次残留开始。
 */
class MaterialDatabaseTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        QStandardPaths::setTestModeEnabled(true);
        m_db = std::make_unique<MaterialDatabase>();
        m_db->clear();
    }

    void TearDown() override
    {
        m_db.reset();
    }

    MaterialDatabase& db() { return *m_db; }

    std::unique_ptr<MaterialDatabase> m_db;
};

// 测试添加材料
TEST_F(MaterialDatabaseTest, AddMaterial_Succeeds)
{
    MaterialDatabase::MaterialInfo info;
    info.name = "TestMaterial";
    info.category = "Custom";
    info.thickness = 1.0;
    info.recommendPower = 50.0;
    info.recommendSpeed = 100.0;

    bool result = db().addMaterial(info);
    EXPECT_TRUE(result);
}

// 测试重复添加：同名同厚度视为同一条，必须拒绝而不是静默覆盖
TEST_F(MaterialDatabaseTest, AddMaterial_RejectsDuplicateNameAndThickness)
{
    MaterialDatabase::MaterialInfo info;
    info.name = "TestMaterial";
    info.thickness = 1.0;

    ASSERT_TRUE(db().addMaterial(info));
    EXPECT_FALSE(db().addMaterial(info));
}

// 测试获取材料
TEST_F(MaterialDatabaseTest, GetMaterial_ReturnsCorrectInfo)
{
    MaterialDatabase::MaterialInfo info;
    info.name = "TestMaterial";
    info.category = "Custom";
    info.thickness = 2.0;
    info.recommendPower = 60.0;

    db().addMaterial(info);

    MaterialDatabase::MaterialInfo retrieved = db().getMaterial("TestMaterial", 2.0);
    EXPECT_EQ(retrieved.name, "TestMaterial");
    EXPECT_DOUBLE_EQ(retrieved.thickness, 2.0);
    EXPECT_DOUBLE_EQ(retrieved.recommendPower, 60.0);
}

// 测试删除材料
TEST_F(MaterialDatabaseTest, RemoveMaterial_Succeeds)
{
    MaterialDatabase::MaterialInfo info;
    info.name = "TestMaterial";
    info.thickness = 1.0;
    db().addMaterial(info);

    bool result = db().removeMaterial("TestMaterial", 1.0);
    EXPECT_TRUE(result);

    MaterialDatabase::MaterialInfo retrieved = db().getMaterial("TestMaterial", 1.0);
    EXPECT_TRUE(retrieved.name.isEmpty());
}

// 测试更新材料
TEST_F(MaterialDatabaseTest, UpdateMaterial_Succeeds)
{
    MaterialDatabase::MaterialInfo info;
    info.name = "TestMaterial";
    info.thickness = 1.0;
    info.recommendPower = 50.0;
    db().addMaterial(info);

    MaterialDatabase::MaterialInfo updatedInfo;
    updatedInfo.name = "TestMaterial";
    updatedInfo.thickness = 1.0;
    updatedInfo.recommendPower = 70.0;

    bool result = db().updateMaterial("TestMaterial", 1.0, updatedInfo);
    EXPECT_TRUE(result);

    MaterialDatabase::MaterialInfo retrieved = db().getMaterial("TestMaterial", 1.0);
    EXPECT_DOUBLE_EQ(retrieved.recommendPower, 70.0);
}

// 测试按类别查询
TEST_F(MaterialDatabaseTest, GetMaterialsByCategory_ReturnsCorrectList)
{
    MaterialDatabase::MaterialInfo info1;
    info1.name = "Material1";
    info1.category = "Metal";
    info1.thickness = 1.0;
    db().addMaterial(info1);

    MaterialDatabase::MaterialInfo info2;
    info2.name = "Material2";
    info2.category = "Metal";
    info2.thickness = 2.0;
    db().addMaterial(info2);

    MaterialDatabase::MaterialInfo info3;
    info3.name = "Material3";
    info3.category = "Wood";
    info3.thickness = 1.0;
    db().addMaterial(info3);

    QList<MaterialDatabase::MaterialInfo> metalMaterials = db().getMaterialsByCategory("Metal");
    EXPECT_EQ(metalMaterials.size(), 2);
}

// 测试获取所有类别
TEST_F(MaterialDatabaseTest, GetCategories_ReturnsAllCategories)
{
    MaterialDatabase::MaterialInfo info1;
    info1.name = "Material1";
    info1.category = "Metal";
    info1.thickness = 1.0;
    db().addMaterial(info1);

    MaterialDatabase::MaterialInfo info2;
    info2.name = "Material2";
    info2.category = "Wood";
    info2.thickness = 1.0;
    db().addMaterial(info2);

    QStringList categories = db().getCategories();
    EXPECT_GE(categories.size(), 2);
    EXPECT_TRUE(categories.contains("Metal"));
    EXPECT_TRUE(categories.contains("Wood"));
}

// 测试加载内置材料
TEST_F(MaterialDatabaseTest, LoadBuiltinMaterials_PopulatesDatabase)
{
    db().loadBuiltinMaterials();

    QList<MaterialDatabase::MaterialInfo> allMaterials = db().getAllMaterials();
    EXPECT_GT(allMaterials.size(), 0);
}

// 测试清空数据库
TEST_F(MaterialDatabaseTest, Clear_RemovesAllMaterials)
{
    MaterialDatabase::MaterialInfo info;
    info.name = "TestMaterial";
    info.thickness = 1.0;
    db().addMaterial(info);

    db().clear();

    QList<MaterialDatabase::MaterialInfo> allMaterials = db().getAllMaterials();
    EXPECT_EQ(allMaterials.size(), 0);
}

// 测试材料类别常量
TEST_F(MaterialDatabaseTest, CategoryConstants_AreDefined)
{
    EXPECT_STREQ(MaterialDatabase::CATEGORY_METAL, "Metal");
    EXPECT_STREQ(MaterialDatabase::CATEGORY_PLASTIC, "Plastic");
    EXPECT_STREQ(MaterialDatabase::CATEGORY_WOOD, "Wood");
    EXPECT_STREQ(MaterialDatabase::CATEGORY_ACRYLIC, "Acrylic");
}

// 测试推荐参数
TEST_F(MaterialDatabaseTest, GetRecommendedSettings_ReturnsSettings)
{
    MaterialDatabase::MaterialInfo info;
    info.name = "TestMaterial";
    info.thickness = 1.0;
    info.recommendPower = 50.0;
    info.recommendSpeed = 100.0;
    info.recommendFrequency = 20.0;
    info.recommendPasses = 1;
    db().addMaterial(info);

    MaterialDatabase::MaterialInfo settings = db().getRecommendedSettings("TestMaterial", 1.0);
    EXPECT_DOUBLE_EQ(settings.recommendPower, 50.0);
    EXPECT_DOUBLE_EQ(settings.recommendSpeed, 100.0);
}
