/**
 * @file CrossImageRttiTests.cpp
 * @brief 跨镜像 RTTI 回归测试
 *
 * 为什么需要这个测试：
 *   全仓库开着 -fvisibility=hidden（根 CMakeLists.txt）。在这种设置下，
 *   一个多态类的 typeinfo 想被多个 .dylib/.so 共用，必须同时满足两个条件：
 *     1) 导出宏在**导入侧也**展开成 visibility("default")
 *        —— 否则消费侧那份 typeinfo 是 private extern；
 *     2) 类有 key function（至少一个非 inline 的虚函数定义）
 *        —— 否则每个 TU 各发一份 weak typeinfo，仍会被压成 private extern，
 *        即使类上标了导出宏也一样（实测结论，不是理论推导）。
 *   任一条不满足，「在 Engine3D 里 new 出来、在别的模块里 dynamic_cast」
 *   就会**静默**返回 nullptr：编译不报错、链接不报错，只在运行时表现为
 *   某条业务链路莫名失败。
 *
 *   真实事故：UI3D 的 FileOperations3D 用 dynamic_cast<SyMeshEntity*> 判定
 *   FioEntityConverter 的产物，OBJ 菜单导入 100% 失败（日志只有
 *   "load model failed: entities=1"），而走 ImportService（按 eType 分拣）
 *   的拖拽导入完全正常。根因是 Engine3DAPI.h / EngineAPI.h 的非 Windows 分支
 *   按 EXPORTS 分叉，且 SyMeshEntity 当时全部虚函数都是 inline。
 *
 * 本测试的有效性依赖一个前提：MainTests 与 Engine3D 是**两个镜像**
 *   （MainTests 链接 libEngine3D.dylib）。所以这里的 dynamic_cast 走的正是
 *   当年出事的那条跨镜像路径。放在 MainTests 而不是 Engine3DTests，是因为
 *   BUILD_ENGINE3D_TESTS 默认关闭 —— 守卫必须待在默认会构建的目标里。
 */

#include <gtest/gtest.h>

#include "Engine3D/Import/FioEntityConverter.h"
#include "Engine3D/SyEntity/SyMeshEntity.h"
#include "Engine/SyEntity/SyEntity.h"
#include "FileIO/FioTypes.h"

#include <cstring>
#include <memory>
#include <vector>

namespace
{
    /// 造一个最小的 Mesh3D IR：一个三角形。
    /// 扩展块布局与 IrProjector 一致：[顶点 n*3 float][法线 n*3 float]
    struct MinimalMeshIr
    {
        Fio::EntityInfo info;
        std::vector<float> blobBytes;
        Fio::BinaryBlob blob;

        MinimalMeshIr()
        {
            blobBytes = {
                // 顶点
                0.0f, 0.0f, 0.0f,
                1.0f, 0.0f, 0.0f,
                0.0f, 1.0f, 0.0f,
                // 法线
                0.0f, 0.0f, 1.0f,
                0.0f, 0.0f, 1.0f,
                0.0f, 0.0f, 1.0f
            };

            info.type = Fio::EntityType::Mesh3D;
            std::strncpy(info.name, "rtti_probe", sizeof(info.name) - 1);
            info.meshVertCount = 3;
            info.meshTriCount = 1;
            info.extensionDataOffset = 0;
            info.extensionDataSize = static_cast<uint32_t>(blobBytes.size() * sizeof(float));

            blob.data = reinterpret_cast<uint8_t*>(blobBytes.data());
            blob.size = blobBytes.size() * sizeof(float);
        }
    };
}  // namespace

// 对象在 Engine3D 镜像内创建，dynamic_cast 在 MainTests 镜像内执行。
// 失败即说明 SyMeshEntity 的 typeinfo 没有跨镜像合并。
TEST(CrossImageRtti, DynamicCastToMeshEntityWorksAcrossImages)
{
    MinimalMeshIr ir;

    std::unique_ptr<Eg::SyEntity> entity = Eg::FioEntityConverter::convertEntity(ir.info, ir.blob);
    ASSERT_NE(entity, nullptr);

    auto* mesh = dynamic_cast<Eg::SyMeshEntity*>(entity.get());
    ASSERT_NE(mesh, nullptr)
        << "dynamic_cast across the module boundary returned nullptr: typeinfo for Eg::SyMeshEntity "
           "is not shared between MainTests and Engine3D. Check two things: ENGINE3D_API / ENGINE_API "
           "must expand to visibility(\"default\") on the consumer side too, and SyMeshEntity must keep "
           "an out-of-line key function (Engine/3D/Src/SyEntity/SyMeshEntity.cpp).";

    EXPECT_EQ(mesh->vertices.size(), 3u);
    EXPECT_EQ(mesh->normals.size(), 3u);
}

// eType 判定是不依赖 RTTI 的备用路线（ImportService / FileOperations3D 都走这条）。
// 它必须与 dynamic_cast 的结论一致，否则两条分拣口径会在同一份数据上分叉。
TEST(CrossImageRtti, ETypeMatchesDynamicCastVerdict)
{
    MinimalMeshIr ir;

    std::unique_ptr<Eg::SyEntity> entity = Eg::FioEntityConverter::convertEntity(ir.info, ir.blob);
    ASSERT_NE(entity, nullptr);

    EXPECT_EQ(entity->eType, Eg::EType::MESH);
    EXPECT_EQ(dynamic_cast<Eg::SyMeshEntity*>(entity.get()) != nullptr, entity->eType == Eg::EType::MESH);
}
