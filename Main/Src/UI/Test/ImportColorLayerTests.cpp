/**
 * @file ImportColorLayerTests.cpp
 * @brief 外来格式导入按颜色落层（图层 = 颜色）
 *
 *  - 同色的源图层合并成一个图层；显式色（DXF 真彩/ACI）按自身颜色落层，落层后改为随层色
 *  - 已有同色图层（含内置默认层）直接复用，不新建；同一文件导入两次不重复建层
 *  - 新颜色建一个该颜色的图层，名称取源图层名
 *  - 同色图层被锁定：同色只能有一个图层，图元就近落到别的可用层、保留自身颜色并告警
 *  - 颜色过多：每次最多新建 64 个颜色层，其余就近合并、保留自身颜色并告警
 *  - 没有颜色信息的图元留在当前层
 */
#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <QFile>
#include <QTemporaryDir>

#include "Engine2D/AprEntity/AprLine.h"
#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/Edit/SceneEditService.h"
#include "Engine2D/Edit/UndoRedoManager.h"
#include "Engine2D/Interaction/LayerManager.h"
#include "Color/Color.hpp"

#include "Import/IImportReader.h"
#include "Import/ImportDispatcher.h"
#include "Import/ImportService.h"

// Unity 构建会把多个测试源合并成一个 TU，辅助代码放进具名命名空间。
namespace ImportColorLayers
{
    /// 一条源记录：x 用来事后认出图元；sourceLayer 为 0 表示不带源图层；hasOwn 时带显式色
    struct Item
    {
        double x = 0.0;
        uint32_t sourceLayer = 0;
        bool hasOwn = false;
        Ut::Color own;
    };

    struct SourceLayer
    {
        uint32_t id;
        std::string name;
        uint32_t argb;
        bool visible = true;
    };

    class ColorReader : public IImportReader
    {
    public:
        ColorReader(std::vector<SourceLayer> layers, std::vector<Item> items)
            : m_layers(std::move(layers))
            , m_items(std::move(items))
        {
        }

        Fio::FileFormat format() const override
        {
            return Fio::FileFormat::DXF;
        }
        QStringList supportedExtensions() const override
        {
            return { QStringLiteral("dxf") };
        }
        ImportDimension dimension() const override
        {
            return ImportDimension::Dim2D;
        }
        QString formatName() const override
        {
            return QStringLiteral("ColorDxf");
        }
        ImportResult read(const ImportContext& /*context*/, Fio::VecAprEntityPtr& out) override
        {
            ImportResult r = ImportResult::ok(QStringLiteral("ok"), static_cast<int>(m_items.size()));
            for (const SourceLayer& l : m_layers)
            {
                Fio::IrLayerInfo info;
                info.sourceId = l.id;
                std::strncpy(info.name, l.name.c_str(), sizeof(info.name) - 1);
                info.color = l.argb;
                info.visible = l.visible;
                r.importedLayers.push_back(info);
            }
            for (const Item& item : m_items)
            {
                auto line = std::make_unique<Eg::AprLine>();
                line->setPointVector({ Ut::Vec2d(item.x, 0.0), Ut::Vec2d(item.x + 1.0, 1.0) });
                if (item.hasOwn)
                {
                    line->setOverrideColor(item.own);
                }
                if (item.sourceLayer != 0)
                {
                    r.entityLayerMap[static_cast<int64_t>(line->id)] = item.sourceLayer;
                }
                out.push_back(std::move(line));
            }
            return r;
        }

    private:
        std::vector<SourceLayer> m_layers;
        std::vector<Item> m_items;
    };

    struct Harness
    {
        Eg::SceneManager scene;
        LayerManager layers{ &scene };
        UndoRedoManager undo{ &scene };
        SceneEditService edit{ &scene, &undo };
        QTemporaryDir dir;
        QString path;

        Harness()
        {
            edit.setLayerManager(&layers);
            path = dir.filePath(QStringLiteral("input.dxf"));
            QFile file(path);
            if (file.open(QIODevice::WriteOnly))
            {
                file.close();
            }
        }

        ImportResult import(const std::vector<SourceLayer>& srcLayers, const std::vector<Item>& items)
        {
            ImportDispatcher dispatcher;
            dispatcher.registerReader(std::make_unique<ColorReader>(srcLayers, items));
            ImportService service;
            service.setDispatcher(&dispatcher);
            service.setSceneManager(&scene);
            service.setLayerManager(&layers);
            service.setEditService(&edit);
            ImportContext context;
            context.sourcePath = path;
            context.preserveLayers = true;
            ImportOptions options;
            options.autoFit = false;
            options.autoCenter = false;
            options.autoSwitchWorkbench = false;
            options.importAsNewDocument = false;
            return service.importWithContext(context, options);
        }

        Eg::AprEntity* entityAt(double x)
        {
            for (Eg::AprEntity* e : scene.getAllEntities())
            {
                auto* line = dynamic_cast<Eg::AprLine*>(e);
                if (line && !line->pointRef().empty() && line->pointRef().front().x() == x)
                {
                    return e;
                }
            }
            return nullptr;
        }
    };

    Item onLayer(double x, uint32_t layer)
    {
        Item i;
        i.x = x;
        i.sourceLayer = layer;
        return i;
    }

    Item withOwnColor(double x, uint32_t layer, const Ut::Color& c)
    {
        Item i = onLayer(x, layer);
        i.hasOwn = true;
        i.own = c;
        return i;
    }

    bool sameRgb(const Ut::Color& a, const Ut::Color& b)
    {
        return a.red255() == b.red255() && a.green255() == b.green255() && a.blue255() == b.blue255();
    }
}  // namespace ImportColorLayers


TEST(ImportColorLayerTest, SameColorSourceLayersMergeIntoExistingColorLayers)
{
    using namespace ImportColorLayers;
    Harness h;
    const int layersBefore = h.layers.layerCount();
    // 默认层：Layer 1 红、Layer 2 绿、Layer 3 蓝
    const ImportResult r = h.import({ { 1, "A", 0xFFFF0000 }, { 2, "B", 0xFFFF0000 }, { 3, "C", 0xFF0000FF } },
        { onLayer(0.0, 1), onLayer(10.0, 1), onLayer(20.0, 2), onLayer(30.0, 3),
            withOwnColor(40.0, 1, Ut::Color::fromRGB255(0, 255, 0)) });
    ASSERT_TRUE(r.success) << r.message.toStdString();

    EXPECT_EQ(h.layers.layerCount(), layersBefore) << "every color already had a layer";
    const int red = h.layers.getEntityLayer(h.entityAt(0.0));
    EXPECT_EQ(red, 1);
    EXPECT_EQ(h.layers.getEntityLayer(h.entityAt(10.0)), red);
    EXPECT_EQ(h.layers.getEntityLayer(h.entityAt(20.0)), red) << "source layers A and B share a color";
    EXPECT_EQ(h.layers.getEntityLayer(h.entityAt(30.0)), 3);

    Eg::AprEntity* green = h.entityAt(40.0);
    EXPECT_EQ(h.layers.getEntityLayer(green), 2) << "explicit color decides the layer";
    EXPECT_FALSE(green->overrideColor().has_value()) << "explicit color becomes by-layer after placement";

    std::string problem;
    EXPECT_TRUE(h.layers.verifyConsistency(&problem)) << problem;
}

TEST(ImportColorLayerTest, NewColorCreatesOneNamedLayerAndReimportReusesIt)
{
    using namespace ImportColorLayers;
    Harness h;
    const int layersBefore = h.layers.layerCount();
    const std::vector<SourceLayer> src = { { 7, "Engrave", 0xFF0C2238 } };  // (12,34,56)

    ASSERT_TRUE(h.import(src, { onLayer(0.0, 7), onLayer(10.0, 7) }).success);
    EXPECT_EQ(h.layers.layerCount(), layersBefore + 1);
    const int layerId = h.layers.getEntityLayer(h.entityAt(0.0));
    EXPECT_EQ(h.layers.layerName(layerId), "Engrave");
    EXPECT_TRUE(sameRgb(h.layers.layerColor(layerId), Ut::Color::fromRGB255(12, 34, 56)));

    ASSERT_TRUE(h.import(src, { onLayer(100.0, 7) }).success);
    EXPECT_EQ(h.layers.layerCount(), layersBefore + 1) << "same color must not create a second layer";
    EXPECT_EQ(h.layers.getEntityLayer(h.entityAt(100.0)), layerId);
}

TEST(ImportColorLayerTest, LockedSameColorLayerSnapsToNearestAndWarns)
{
    using namespace ImportColorLayers;
    Harness h;
    ASSERT_TRUE(h.layers.setLayerLocked(1, true));  // 红色默认层被锁
    const int layersBefore = h.layers.layerCount();
    const ImportResult r = h.import({ { 1, "Cut", 0xFFFF0000 } }, { onLayer(0.0, 1) });
    ASSERT_TRUE(r.success);

    Eg::AprEntity* entity = h.entityAt(0.0);
    const int placed = h.layers.getEntityLayer(entity);
    EXPECT_NE(placed, 1);
    EXPECT_FALSE(h.layers.isLayerLocked(placed));
    // 不另建同色层：红色仍只属于被锁的 1 号层
    EXPECT_EQ(h.layers.layerCount(), layersBefore);
    EXPECT_EQ(h.layers.findLayerByColor(Ut::Color::fromRGB255(255, 0, 0)), 1);
    // 图元保留自己的红色显示
    EXPECT_TRUE(sameRgb(entity->getColor(), Ut::Color::fromRGB255(255, 0, 0)));
    EXPECT_FALSE(r.warnings.isEmpty());
}

TEST(ImportColorLayerTest, TooManyColorsAreCappedAndSnapped)
{
    using namespace ImportColorLayers;
    Harness h;
    const int layersBefore = h.layers.layerCount();
    std::vector<Item> items;
    for (int i = 0; i < 100; ++i)
    {
        // 避开默认层颜色的 100 种颜色
        items.push_back(withOwnColor(i * 10.0, 0, Ut::Color::fromRGB255(10 + i, 200 - i, 77)));
    }
    const ImportResult r = h.import({}, items);
    ASSERT_TRUE(r.success);

    EXPECT_EQ(h.layers.layerCount(), layersBefore + 64);
    EXPECT_FALSE(r.warnings.isEmpty());
    int keptOwnColor = 0;
    for (int i = 0; i < 100; ++i)
    {
        Eg::AprEntity* e = h.entityAt(i * 10.0);
        ASSERT_NE(e, nullptr);
        EXPECT_EQ(h.layers.layerType(h.layers.getEntityLayer(e)), Eg::LayerType::VECTOR);
        if (e->overrideColor().has_value())
        {
            ++keptOwnColor;
        }
    }
    EXPECT_EQ(keptOwnColor, 36) << "snapped entities keep their own display color";
}

TEST(ImportColorLayerTest, EntitiesWithoutColorStayOnCurrentLayer)
{
    using namespace ImportColorLayers;
    Harness h;
    const int current = h.layers.createLayer("Current");
    ASSERT_TRUE(h.layers.setCurrentLayer(current));
    const int layersBefore = h.layers.layerCount();

    ASSERT_TRUE(h.import({}, { onLayer(0.0, 0) }).success);
    EXPECT_EQ(h.layers.getEntityLayer(h.entityAt(0.0)), current);
    EXPECT_EQ(h.layers.layerCount(), layersBefore);
}
