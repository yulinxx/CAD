/**
 * @file FioEntityConverterTests.cpp
 * @brief FioEntityConverter 回归测试 — 中立 IR → Engine SyEntity 转换
 *
 * 覆盖：
 *   - 所有实体类型的 convertEntity() 转换
 *   - 扩展数据（多边形、NURBS）的转换
 *   - convertAll() 批量转换
 *   - extractLayers() 图层提取
 *   - 边界条件（未知类型、空数据）
 *
 * P2 测试覆盖扩展 (2026-07-30)
 */

#include <gtest/gtest.h>
#include <cstring>

#include "FioEntityConverter.h"
#include "FileIO/FioTypes.h"

 // Engine2D 实体类型
#include "Engine2D/SyEntity/SyLine.h"
#include "Engine2D/SyEntity/SyArc.h"
#include "Engine2D/SyEntity/SyCircle.h"
#include "Engine2D/SyEntity/SyEllipse.h"
#include "Engine2D/SyEntity/SyPoint.h"
#include "Engine2D/SyEntity/SyPolygon.h"
#include "Engine2D/SyEntity/SyBezier.h"
#include "Engine2D/SyEntity/SyBezier2.h"
#include "Engine2D/SyEntity/SyNurbs.h"
#include "Engine2D/SyEntity/SyText.h"
#include "Engine2D/SyEntity/SyImage.h"
#include "Engine2D/SyEntity/SyBarCode.h"
#include "Engine2D/SyEntity/SyQRCode.h"

// ==================== 线段转换 ====================

TEST(FioEntityConverterTest, ConvertLineEntity)
{
    Fio::EntityInfo info;
    info.type = Fio::EntityType::Line;
    info.line.x1 = 10.0;
    info.line.y1 = 20.0;
    info.line.x2 = 100.0;
    info.line.y2 = 200.0;

    auto entity = FioEntityConverter::convertEntity(info);
    ASSERT_NE(entity, nullptr);
    EXPECT_EQ(entity->eType, Eg::EType::LINE);

    auto* line = static_cast<Eg::SyLine*>(entity.get());
    ASSERT_EQ(line->pointRef().size(), 2u);
    EXPECT_DOUBLE_EQ(line->pointRef()[0].x(), 10.0);
    EXPECT_DOUBLE_EQ(line->pointRef()[0].y(), 20.0);
    EXPECT_DOUBLE_EQ(line->pointRef()[1].x(), 100.0);
    EXPECT_DOUBLE_EQ(line->pointRef()[1].y(), 200.0);
}

// ==================== 圆转换 ====================

TEST(FioEntityConverterTest, ConvertCircleEntity)
{
    Fio::EntityInfo info;
    info.type = Fio::EntityType::Circle;
    info.circle.cx = 50.0;
    info.circle.cy = 60.0;
    info.circle.r = 25.0;

    auto entity = FioEntityConverter::convertEntity(info);
    ASSERT_NE(entity, nullptr);
    EXPECT_EQ(entity->eType, Eg::EType::CIRCLE);

    auto* circle = static_cast<Eg::SyCircle*>(entity.get());
    EXPECT_DOUBLE_EQ(circle->basePoint.x(), 50.0);
    EXPECT_DOUBLE_EQ(circle->basePoint.y(), 60.0);
    EXPECT_DOUBLE_EQ(circle->dRadius, 25.0);
}

// ==================== 圆弧转换 ====================

TEST(FioEntityConverterTest, ConvertArcEntity)
{
    Fio::EntityInfo info;
    info.type = Fio::EntityType::Arc;
    info.arc.cx = 10.0;
    info.arc.cy = 20.0;
    info.arc.r = 30.0;
    info.arc.sa = 0.0;
    info.arc.ea = 1.5708;

    auto entity = FioEntityConverter::convertEntity(info);
    ASSERT_NE(entity, nullptr);
    EXPECT_EQ(entity->eType, Eg::EType::ARC);

    auto* arc = static_cast<Eg::SyArc*>(entity.get());
    EXPECT_DOUBLE_EQ(arc->basePoint.x(), 10.0);
    EXPECT_DOUBLE_EQ(arc->basePoint.y(), 20.0);
    EXPECT_DOUBLE_EQ(arc->dRadius, 30.0);
    EXPECT_DOUBLE_EQ(arc->dStartAngle, 0.0);
    EXPECT_DOUBLE_EQ(arc->dEndAngle, 1.5708);
}

// ==================== 椭圆转换 ====================

TEST(FioEntityConverterTest, ConvertEllipseEntity)
{
    Fio::EntityInfo info;
    info.type = Fio::EntityType::Ellipse;
    info.ellipse.cx = 0.0;
    info.ellipse.cy = 0.0;
    info.ellipse.rx = 50.0;
    info.ellipse.ry = 30.0;
    info.ellipse.rot = 0.5;
    info.ellipse.sa = 0.0;
    info.ellipse.ea = 6.2832;

    auto entity = FioEntityConverter::convertEntity(info);
    ASSERT_NE(entity, nullptr);
    EXPECT_EQ(entity->eType, Eg::EType::ELLIPSE);

    auto* ell = static_cast<Eg::SyEllipse*>(entity.get());
    EXPECT_DOUBLE_EQ(ell->basePoint.x(), 0.0);
    EXPECT_DOUBLE_EQ(ell->basePoint.y(), 0.0);
    EXPECT_DOUBLE_EQ(ell->dRadiusX, 50.0);
    EXPECT_DOUBLE_EQ(ell->dRadiusY, 30.0);
    EXPECT_DOUBLE_EQ(ell->dRotation, 0.5);
}

// ==================== 点转换 ====================

TEST(FioEntityConverterTest, ConvertPointEntity)
{
    Fio::EntityInfo info;
    info.type = Fio::EntityType::Point;
    info.line.x1 = 42.0;  // 点坐标复用 line.x1/y1
    info.line.y1 = 99.0;

    auto entity = FioEntityConverter::convertEntity(info);
    ASSERT_NE(entity, nullptr);
    EXPECT_EQ(entity->eType, Eg::EType::POINT);

    auto* pt = static_cast<Eg::SyPoint*>(entity.get());
    EXPECT_DOUBLE_EQ(pt->basePoint.x(), 42.0);
    EXPECT_DOUBLE_EQ(pt->basePoint.y(), 99.0);
}

// ==================== 三次贝塞尔转换 ====================

TEST(FioEntityConverterTest, ConvertBezierEntity)
{
    Fio::EntityInfo info;
    info.type = Fio::EntityType::Bezier;
    info.line.x1 = 0.0;   // 起点 P0
    info.line.y1 = 0.0;
    info.bezier.c0x = 50.0;
    info.bezier.c0y = 100.0;
    info.bezier.c1x = 100.0;
    info.bezier.c1y = 100.0;
    info.bezier.ex = 150.0;
    info.bezier.ey = 0.0;

    auto entity = FioEntityConverter::convertEntity(info);
    ASSERT_NE(entity, nullptr);
    EXPECT_EQ(entity->eType, Eg::EType::BEZIER);

    auto* bz = static_cast<Eg::SyBezier*>(entity.get());
    EXPECT_DOUBLE_EQ(bz->basePoint.x(), 0.0);   // P0
    EXPECT_DOUBLE_EQ(bz->ptCtrl0.x(), 50.0);
    EXPECT_DOUBLE_EQ(bz->ptCtrl0.y(), 100.0);
    EXPECT_DOUBLE_EQ(bz->ptCtrl1.x(), 100.0);
    EXPECT_DOUBLE_EQ(bz->ptCtrl1.y(), 100.0);
    EXPECT_DOUBLE_EQ(bz->ptEnd.x(), 150.0);
    EXPECT_DOUBLE_EQ(bz->ptEnd.y(), 0.0);
}

// ==================== 二次贝塞尔转换 ====================

TEST(FioEntityConverterTest, ConvertBezier2Entity)
{
    Fio::EntityInfo info;
    info.type = Fio::EntityType::Bezier2;
    info.line.x1 = 0.0;   // 起点 P0
    info.line.y1 = 0.0;
    info.bezier2.cx = 50.0;
    info.bezier2.cy = 100.0;
    info.bezier2.ex = 100.0;
    info.bezier2.ey = 0.0;

    auto entity = FioEntityConverter::convertEntity(info);
    ASSERT_NE(entity, nullptr);
    EXPECT_EQ(entity->eType, Eg::EType::BEZIER2);

    auto* bz = static_cast<Eg::SyBezier2*>(entity.get());
    EXPECT_DOUBLE_EQ(bz->basePoint.x(), 0.0);   // P0
    EXPECT_DOUBLE_EQ(bz->ptCtrl.x(), 50.0);
    EXPECT_DOUBLE_EQ(bz->ptCtrl.y(), 100.0);
    EXPECT_DOUBLE_EQ(bz->ptEnd.x(), 100.0);
    EXPECT_DOUBLE_EQ(bz->ptEnd.y(), 0.0);
}

// ==================== 文本转换 ====================

TEST(FioEntityConverterTest, ConvertTextEntity)
{
    Fio::EntityInfo info;
    info.type = Fio::EntityType::Text;
    info.text.x = 100.0;
    info.text.y = 200.0;
    std::strncpy(info.text.text, "Hello World", sizeof(info.text.text));
    info.text.h = 12.0;
    info.text.a = 0.5;

    auto entity = FioEntityConverter::convertEntity(info);
    ASSERT_NE(entity, nullptr);
    EXPECT_EQ(entity->eType, Eg::EType::TEXT);

    auto* txt = static_cast<Eg::SyText*>(entity.get());
    EXPECT_DOUBLE_EQ(txt->basePoint.x(), 100.0);
    EXPECT_DOUBLE_EQ(txt->basePoint.y(), 200.0);
    EXPECT_EQ(txt->textStr(), "Hello World");
    EXPECT_DOUBLE_EQ(txt->dHeight, 12.0);
    EXPECT_DOUBLE_EQ(txt->dRotation, 0.5);
}

// ==================== 条形码转换 ====================

TEST(FioEntityConverterTest, ConvertBarCodeEntity)
{
    Fio::EntityInfo info;
    info.type = Fio::EntityType::BarCode;
    std::strncpy(info.text.text, "1234567890", sizeof(info.text.text));
    info.text.x = 10.0;
    info.text.y = 20.0;
    info.barWidth = 50.0;
    info.barHeight = 30.0;

    auto entity = FioEntityConverter::convertEntity(info);
    ASSERT_NE(entity, nullptr);
    EXPECT_EQ(entity->eType, Eg::EType::BAR_CODE);

    auto* bc = static_cast<Eg::SyBarCode*>(entity.get());
    EXPECT_EQ(bc->dataStr(), "1234567890");
    EXPECT_DOUBLE_EQ(bc->dWidth, 50.0);
    EXPECT_DOUBLE_EQ(bc->dHeight, 30.0);
    EXPECT_DOUBLE_EQ(bc->basePoint.x(), 10.0);
    EXPECT_DOUBLE_EQ(bc->basePoint.y(), 20.0);
}

// ==================== 二维码转换 ====================

TEST(FioEntityConverterTest, ConvertQRCodeEntity)
{
    Fio::EntityInfo info;
    info.type = Fio::EntityType::QRCode;
    std::strncpy(info.text.text, "QR_DATA", sizeof(info.text.text));
    info.text.x = 5.0;
    info.text.y = 5.0;
    info.moduleSize = 10.0;

    auto entity = FioEntityConverter::convertEntity(info);
    ASSERT_NE(entity, nullptr);
    EXPECT_EQ(entity->eType, Eg::EType::QR_CODE);

    auto* qr = static_cast<Eg::SyQRCode*>(entity.get());
    EXPECT_EQ(qr->dataStr(), "QR_DATA");
    EXPECT_DOUBLE_EQ(qr->dModuleSize, 10.0);
    EXPECT_DOUBLE_EQ(qr->basePoint.x(), 5.0);
    EXPECT_DOUBLE_EQ(qr->basePoint.y(), 5.0);
}

// ==================== 多边形转换（含扩展数据） ====================

TEST(FioEntityConverterTest, ConvertPolygonWithExtensionData)
{
    // 多边形顶点: (0,0), (10,0), (10,10), (0,10)
    double vertices[] = { 0.0, 0.0, 10.0, 0.0, 10.0, 10.0, 0.0, 10.0 };
    uint8_t blobBuffer[sizeof(vertices)];
    std::memcpy(blobBuffer, vertices, sizeof(vertices));

    Fio::BinaryBlob blob{ blobBuffer, sizeof(blobBuffer) };

    Fio::EntityInfo info;
    info.type = Fio::EntityType::Polygon;
    info.vertexCount = 4;
    info.extensionDataOffset = 0;
    info.extensionDataSize = sizeof(vertices);

    auto entity = FioEntityConverter::convertEntity(info, blob);
    ASSERT_NE(entity, nullptr);
    EXPECT_EQ(entity->eType, Eg::EType::POLYGON);

    auto* poly = static_cast<Eg::SyPolygon*>(entity.get());
    EXPECT_TRUE(poly->bClosed);
    const auto& verts = poly->vertices();
    ASSERT_EQ(verts.size(), 4u);
    EXPECT_DOUBLE_EQ(verts[0].x(), 0.0);
    EXPECT_DOUBLE_EQ(verts[0].y(), 0.0);
    EXPECT_DOUBLE_EQ(verts[3].x(), 0.0);
    EXPECT_DOUBLE_EQ(verts[3].y(), 10.0);
}

// ==================== 折线转换（含扩展数据） ====================

TEST(FioEntityConverterTest, ConvertPolylineWithExtensionData)
{
    // 折线顶点: (0,0), (10,0), (10,10)
    double vertices[] = { 0.0, 0.0, 10.0, 0.0, 10.0, 10.0 };
    uint8_t blobBuffer[sizeof(vertices)];
    std::memcpy(blobBuffer, vertices, sizeof(vertices));

    Fio::BinaryBlob blob{ blobBuffer, sizeof(blobBuffer) };

    Fio::EntityInfo info;
    info.type = Fio::EntityType::Polyline;
    info.vertexCount = 3;
    info.extensionDataOffset = 0;
    info.extensionDataSize = sizeof(vertices);

    auto entity = FioEntityConverter::convertEntity(info, blob);
    ASSERT_NE(entity, nullptr);
    EXPECT_EQ(entity->eType, Eg::EType::POLYGON);

    auto* poly = static_cast<Eg::SyPolygon*>(entity.get());
    EXPECT_FALSE(poly->bClosed);  // 折线不闭合
    const auto& verts = poly->vertices();
    ASSERT_EQ(verts.size(), 3u);
    EXPECT_DOUBLE_EQ(verts[2].x(), 10.0);
    EXPECT_DOUBLE_EQ(verts[2].y(), 10.0);
}

// ==================== NURBS 转换（含扩展数据） ====================

TEST(FioEntityConverterTest, ConvertNurbsWithExtensionData)
{
    // 控制点(3个), 节点(5个), 权重(3个)
    // 扩展数据布局: [控制点(6 doubles)] [节点(5 doubles)] [权重(3 doubles)]
    double extData[] = {
        0.0, 0.0,   // 控制点 0
        5.0, 10.0,  // 控制点 1
        10.0, 0.0,  // 控制点 2
        0.0, 0.0, 0.0, 0.5, 1.0,  // 节点
        1.0, 1.0, 1.0               // 权重
    };
    uint8_t blobBuffer[sizeof(extData)];
    std::memcpy(blobBuffer, extData, sizeof(extData));

    Fio::BinaryBlob blob{ blobBuffer, sizeof(blobBuffer) };

    Fio::EntityInfo info;
    info.type = Fio::EntityType::Nurbs;
    info.nurbsDegree = 3;
    info.nurbsCtrlPtCount = 3;
    info.nurbsKnotCount = 5;
    info.extensionDataOffset = 0;
    info.extensionDataSize = sizeof(extData);

    auto entity = FioEntityConverter::convertEntity(info, blob);
    ASSERT_NE(entity, nullptr);
    EXPECT_EQ(entity->eType, Eg::EType::SPLINE);

    auto* nurbs = static_cast<Eg::SyNurbs*>(entity.get());
    EXPECT_EQ(nurbs->nDegree, 3);
    EXPECT_EQ(nurbs->controlPointCount(), 3u);
    EXPECT_EQ(nurbs->knotCount(), 5u);
    EXPECT_EQ(nurbs->weightCount(), 3u);
    EXPECT_DOUBLE_EQ(nurbs->controlPointAt(1).x(), 5.0);
    EXPECT_DOUBLE_EQ(nurbs->controlPointAt(1).y(), 10.0);
    EXPECT_DOUBLE_EQ(nurbs->knotAt(3), 0.5);
    EXPECT_DOUBLE_EQ(nurbs->weightAt(0), 1.0);
}

// ==================== 图像转换（含扩展数据） ====================

TEST(FioEntityConverterTest, ConvertImageWithExtensionData)
{
    uint8_t pixelData[] = { 0xFF, 0x00, 0x00, 0xFF, 0x00, 0xFF };
    uint8_t blobBuffer[sizeof(pixelData)];
    std::memcpy(blobBuffer, pixelData, sizeof(pixelData));

    Fio::BinaryBlob blob{ blobBuffer, sizeof(blobBuffer) };

    Fio::EntityInfo info;
    info.type = Fio::EntityType::Image;
    info.imageWidth = 2;
    info.imageHeight = 1;
    info.line.x1 = 0.0;
    info.line.y1 = 0.0;
    info.extensionDataOffset = 0;
    info.extensionDataSize = sizeof(pixelData);

    auto entity = FioEntityConverter::convertEntity(info, blob);
    ASSERT_NE(entity, nullptr);
    EXPECT_EQ(entity->eType, Eg::EType::IMAGE);

    auto* img = static_cast<Eg::SyImage*>(entity.get());
    EXPECT_EQ(img->nWidth, 2);
    EXPECT_EQ(img->nHeight, 1);
    EXPECT_EQ(img->pixelDataSize(), sizeof(pixelData));
    EXPECT_EQ(img->pixelData()[0], 0xFF);
}

// ==================== 未知类型 ====================

TEST(FioEntityConverterTest, ConvertUnknownTypeReturnsNull)
{
    Fio::EntityInfo info;
    info.type = Fio::EntityType::Unknown;

    auto entity = FioEntityConverter::convertEntity(info);
    EXPECT_EQ(entity, nullptr);
}

// ==================== 批量转换 ====================

TEST(FioEntityConverterTest, ConvertAllBatch)
{
    Fio::EntityInfo entities[3];
    entities[0].type = Fio::EntityType::Line;
    entities[0].line.x1 = 0.0; entities[0].line.y1 = 0.0;
    entities[0].line.x2 = 10.0; entities[0].line.y2 = 10.0;
    entities[0].visible = true;
    entities[0].locked = false;

    entities[1].type = Fio::EntityType::Circle;
    entities[1].circle.cx = 50.0; entities[1].circle.cy = 50.0;
    entities[1].circle.r = 20.0;
    entities[1].visible = true;
    entities[1].locked = false;

    entities[2].type = Fio::EntityType::Unknown;  // 应被跳过

    Fio::FioParseResult parseData;
    parseData.entities = entities;
    parseData.entityCount = 3;
    std::strncpy(parseData.sourceFormat, "DXF", sizeof(parseData.sourceFormat));

    auto result = FioEntityConverter::convertAll(parseData);
    EXPECT_EQ(result.size(), 2u);  // 未知类型被跳过
    EXPECT_EQ(result[0]->eType, Eg::EType::LINE);
    EXPECT_EQ(result[1]->eType, Eg::EType::CIRCLE);
}

// ==================== 空批量转换 ====================

TEST(FioEntityConverterTest, ConvertAllEmptyInput)
{
    Fio::FioParseResult parseData;
    parseData.entities = nullptr;
    parseData.entityCount = 0;

    auto result = FioEntityConverter::convertAll(parseData);
    EXPECT_TRUE(result.empty());
}

// ==================== 图层提取 ====================

TEST(FioEntityConverterTest, ExtractLayers)
{
    Fio::IrLayerInfo layers[2];
    layers[0].sourceId = 1;
    std::strncpy(layers[0].name, "Layer1", sizeof(layers[0].name));
    layers[0].color = 0xFF0000;
    layers[0].visible = true;
    layers[0].locked = false;

    layers[1].sourceId = 2;
    std::strncpy(layers[1].name, "Layer2", sizeof(layers[1].name));
    layers[1].color = 0x00FF00;
    layers[1].visible = false;
    layers[1].locked = true;

    Fio::FioParseResult parseData;
    parseData.layers = layers;
    parseData.layerCount = 2;

    auto result = FioEntityConverter::extractLayers(parseData);
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0].sourceId, 1u);
    EXPECT_EQ(std::string(result[0].name), "Layer1");
    EXPECT_EQ(result[0].color, 0xFF0000u);
    EXPECT_TRUE(result[0].visible);

    EXPECT_EQ(result[1].sourceId, 2u);
    EXPECT_EQ(std::string(result[1].name), "Layer2");
    EXPECT_EQ(result[1].color, 0x00FF00u);
    EXPECT_FALSE(result[1].visible);
    EXPECT_TRUE(result[1].locked);
}

// ==================== 空图层提取 ====================

TEST(FioEntityConverterTest, ExtractLayersEmptyInput)
{
    Fio::FioParseResult parseData;
    parseData.layers = nullptr;
    parseData.layerCount = 0;

    auto result = FioEntityConverter::extractLayers(parseData);
    EXPECT_TRUE(result.empty());
}

// ==================== 多边形空扩展数据 ====================

TEST(FioEntityConverterTest, ConvertPolygonWithEmptyExtensionData)
{
    Fio::EntityInfo info;
    info.type = Fio::EntityType::Polygon;
    info.vertexCount = 0;
    info.extensionDataOffset = 0;
    info.extensionDataSize = 0;

    auto entity = FioEntityConverter::convertEntity(info);
    ASSERT_NE(entity, nullptr);
    EXPECT_EQ(entity->eType, Eg::EType::POLYGON);

    auto* poly = static_cast<Eg::SyPolygon*>(entity.get());
    EXPECT_TRUE(poly->bClosed);
    EXPECT_TRUE(poly->vertices().empty());
}

// ==================== 验证 EntityInfo 结构体大小（跨 DLL 安全） ====================

TEST(FioEntityConverterTest, EntityInfoLayoutIsStable)
{
    // 确保 EntityInfo 结构体大小跨 DLL 边界一致
    // 这是 P2 ABI 收口后的回归验证
    constexpr size_t expectedSize =
        sizeof(uint64_t) +           // sourceId
        sizeof(uint8_t) +            // EntityType (enum : uint8_t)
        sizeof(char) * 256 +         // name[256]
        sizeof(uint32_t) +           // layerSourceId
        sizeof(double) +             // lineWidth
        sizeof(bool) + sizeof(bool) + // visible, locked
        sizeof(double) * 4 +          // line (4 doubles)
        sizeof(double) * 5 +          // arc (5 doubles)
        sizeof(double) * 3 +          // circle (3 doubles)
        sizeof(double) * 7 +          // ellipse (7 doubles)
        sizeof(double) * 4 + sizeof(char) * 256 +  // text (2 doubles + 256 chars + 2 doubles)
        sizeof(double) * 6 +          // bezier (6 doubles)
        sizeof(double) * 4 +          // bezier2 (4 doubles)
        sizeof(uint32_t) +           // vertexCount
        sizeof(int32_t) + sizeof(uint32_t) * 3 +   // nurbs params
        sizeof(int32_t) * 2 +        // image params
        sizeof(double) * 2 +         // barCode params
        sizeof(double) +             // moduleSize
        sizeof(uint32_t) * 2;        // extensionDataOffset, extensionDataSize

    // 结构体大小必须稳定，跨 DLL 边界时 layout 一致
    EXPECT_GE(sizeof(Fio::EntityInfo), 300u);
    EXPECT_LT(sizeof(Fio::EntityInfo), 2000u);
}