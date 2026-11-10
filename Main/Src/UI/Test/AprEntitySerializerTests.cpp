/**
 * @file AprEntitySerializerTests.cpp
 * @brief AprEntitySerializer 往返测试 — AprEntity ↔ Protobuf 序列化封装
 * 覆盖：
 *   - 所有图元类型的 serializeEntity ↔ deserializeEntity 闭环
 *   - 通用属性（id, basePoint, bClosed, bCCW）的序列化
 *   - 视图关联的序列化
 *   - 复杂图元类型（NURBS, Image）的序列化
 * P2 测试覆盖扩展 (2026-07-30)
 */
#include <gtest/gtest.h>

#include "AprEntitySerializer.h"
#include "FileIO/AprDocument.h"

// Engine2D 图元类型
#include "Engine2D/AprEntity/AprLine.h"
#include "Engine2D/AprEntity/AprArc.h"
#include "Engine2D/AprEntity/AprCircle.h"
#include "Engine2D/AprEntity/AprEllipse.h"
#include "Engine2D/AprEntity/AprPoint.h"
#include "Engine2D/AprEntity/AprPolygon.h"
#include "Engine2D/AprEntity/AprBezier.h"
#include "Engine2D/AprEntity/AprBezier2.h"
#include "Engine2D/AprEntity/AprNurbs.h"
#include "Engine2D/AprEntity/AprText.h"
#include "Engine2D/AprEntity/AprImage.h"
#include "Engine2D/AprEntity/AprBarCode.h"
#include "Engine2D/AprEntity/AprQRCode.h"

#include "AprDocument.pb.h"

namespace
{
    /// 辅助：将图元序列化后立即反序列化，验证往返
    template<typename T>
    std::pair<std::unique_ptr<Eg::AprEntity>, apr::proto::EntityData> roundTrip(const Eg::AprEntity& entity)
    {
        apr::proto::EntityData protoData;
        Fio::AprEntitySerializer::serializeEntity(entity, &protoData);

        auto deserialized = Fio::AprEntitySerializer::deserializeEntity(protoData);
        return { std::move(deserialized), std::move(protoData) };
    }

    /// 辅助：验证两个 Vec2d 相等
    void expectVec2dEqual(const Ut::Vec2d& a, const Ut::Vec2d& b)
    {
        EXPECT_DOUBLE_EQ(a.x(), b.x());
        EXPECT_DOUBLE_EQ(a.y(), b.y());
    }
}  // namespace

// ---- 线段序列化闭环 ----

TEST(AprEntitySerializerTest, LineRoundTrip)
{
    Eg::AprLine original;
    original.id = 42;
    original.basePoint = Ut::Vec2d(10.0, 20.0);
    original.bClosed = false;
    original.bCCW = true;
    original.addPoint(Ut::Vec2d(10.0, 20.0));
    original.addPoint(Ut::Vec2d(100.0, 200.0));

    auto [deserialized, proto] = roundTrip<Eg::AprLine>(original);
    ASSERT_NE(deserialized, nullptr);
    EXPECT_EQ(deserialized->eType, Eg::EType::LINE);
    EXPECT_EQ(deserialized->id, 42u);
    auto* lineRt = static_cast<Eg::AprLine*>(deserialized.get());
    expectVec2dEqual(lineRt->basePoint, Ut::Vec2d(10.0, 20.0));
    EXPECT_FALSE(lineRt->bClosed);
    EXPECT_TRUE(lineRt->bCCW);

    auto* line = static_cast<Eg::AprLine*>(deserialized.get());
    ASSERT_EQ(line->pointRef().size(), 2u);
    expectVec2dEqual(line->pointRef()[0], Ut::Vec2d(10.0, 20.0));
    expectVec2dEqual(line->pointRef()[1], Ut::Vec2d(100.0, 200.0));
}

// ---- 圆序列化闭环 ----

TEST(AprEntitySerializerTest, CircleRoundTrip)
{
    Eg::AprCircle original;
    original.id = 7;
    original.basePoint = Ut::Vec2d(50.0, 60.0);
    original.dRadius = 25.0;

    auto [deserialized, proto] = roundTrip<Eg::AprCircle>(original);
    ASSERT_NE(deserialized, nullptr);
    EXPECT_EQ(deserialized->eType, Eg::EType::CIRCLE);
    EXPECT_EQ(deserialized->id, 7u);

    auto* circle = static_cast<Eg::AprCircle*>(deserialized.get());
    expectVec2dEqual(circle->basePoint, Ut::Vec2d(50.0, 60.0));
    EXPECT_DOUBLE_EQ(circle->dRadius, 25.0);
}

// ---- 圆弧序列化闭环 ----

TEST(AprEntitySerializerTest, ArcRoundTrip)
{
    Eg::AprArc original;
    original.id = 3;
    original.basePoint = Ut::Vec2d(10.0, 20.0);
    original.dRadius = 30.0;
    original.dStartAngle = 0.0;
    original.dEndAngle = 1.5708;

    auto [deserialized, proto] = roundTrip<Eg::AprArc>(original);
    ASSERT_NE(deserialized, nullptr);
    EXPECT_EQ(deserialized->eType, Eg::EType::ARC);

    auto* arc = static_cast<Eg::AprArc*>(deserialized.get());
    EXPECT_DOUBLE_EQ(arc->dRadius, 30.0);
    EXPECT_DOUBLE_EQ(arc->dStartAngle, 0.0);
    EXPECT_DOUBLE_EQ(arc->dEndAngle, 1.5708);
}

// ---- 椭圆序列化闭环 ----

TEST(AprEntitySerializerTest, EllipseRoundTrip)
{
    Eg::AprEllipse original;
    original.id = 5;
    original.basePoint = Ut::Vec2d(0.0, 0.0);
    original.dRadiusX = 50.0;
    original.dRadiusY = 30.0;
    original.dRotation = 0.5;

    auto [deserialized, proto] = roundTrip<Eg::AprEllipse>(original);
    ASSERT_NE(deserialized, nullptr);
    EXPECT_EQ(deserialized->eType, Eg::EType::ELLIPSE);

    auto* ell = static_cast<Eg::AprEllipse*>(deserialized.get());
    EXPECT_DOUBLE_EQ(ell->dRadiusX, 50.0);
    EXPECT_DOUBLE_EQ(ell->dRadiusY, 30.0);
    EXPECT_DOUBLE_EQ(ell->dRotation, 0.5);
}

// ---- 点序列化闭环 ----

TEST(AprEntitySerializerTest, PointRoundTrip)
{
    Eg::AprPoint original;
    original.id = 1;
    original.basePoint = Ut::Vec2d(42.0, 99.0);

    auto [deserialized, proto] = roundTrip<Eg::AprPoint>(original);
    ASSERT_NE(deserialized, nullptr);
    EXPECT_EQ(deserialized->eType, Eg::EType::POINT);
    expectVec2dEqual(static_cast<Eg::AprPoint*>(deserialized.get())->basePoint, Ut::Vec2d(42.0, 99.0));
}

// ---- 多段线序列化循环（显式端点） ----

TEST(AprEntitySerializerTest, PolygonRoundTrip)
{
    // 使用 setVertices 设置多边形顶点，避免与参数化定义的冲突
    Eg::AprPolygon original;
    original.id = 10;
    original.basePoint = Ut::Vec2d(0.0, 0.0);
    original.bClosed = true;
    original.bCCW = false;

    original.setVertices({ Ut::Vec2d(0.0, 0.0), Ut::Vec2d(10.0, 0.0), Ut::Vec2d(10.0, 10.0), Ut::Vec2d(0.0, 10.0) });

    auto [deserialized, proto] = roundTrip<Eg::AprPolygon>(original);
    ASSERT_NE(deserialized, nullptr);
    EXPECT_EQ(deserialized->eType, Eg::EType::POLYGON);
    EXPECT_TRUE(static_cast<Eg::AprPolygon*>(deserialized.get())->bClosed);
    EXPECT_FALSE(static_cast<Eg::AprPolygon*>(deserialized.get())->bCCW);

    auto* poly = static_cast<Eg::AprPolygon*>(deserialized.get());
    const auto& resultVerts = poly->vertices();
    ASSERT_EQ(resultVerts.size(), 4u);
    expectVec2dEqual(resultVerts[0], Ut::Vec2d(0.0, 0.0));
    expectVec2dEqual(resultVerts[3], Ut::Vec2d(0.0, 10.0));
}

TEST(AprEntitySerializerTest, BulgePolygonRoundTrip)
{
    Eg::AprPolygon original;
    original.bClosed = false;
    original.setVerticesWithBulges({ Ut::Vec2d(0.0, 0.0), Ut::Vec2d(10.0, 0.0) }, { 1.0, 0.0 });

    auto [deserialized, proto] = roundTrip<Eg::AprPolygon>(original);
    ASSERT_NE(deserialized, nullptr);
    ASSERT_TRUE(proto.has_polygon_data());
    ASSERT_EQ(proto.polygon_data().bulges_size(), 2);

    auto* poly = static_cast<Eg::AprPolygon*>(deserialized.get());
    EXPECT_FALSE(poly->bClosed);
    ASSERT_TRUE(poly->hasBulges());
    ASSERT_EQ(poly->bulges().size(), 2u);
    EXPECT_DOUBLE_EQ(poly->bulges()[0], 1.0);
    EXPECT_DOUBLE_EQ(poly->bulges()[1], 0.0);
    EXPECT_EQ(poly->vertices().size(), 2u);
}

// ---- 多段线序列化循环（参数化） ----

TEST(AprEntitySerializerTest, ParametricPolygonRoundTrip)
{
    // 仅使用参数化定义（nSides + dCircumRadius），不设置显式顶点
    Eg::AprPolygon original;
    original.id = 11;
    original.basePoint = Ut::Vec2d(50.0, 50.0);
    original.nSides = 6;
    original.dCircumRadius = 30.0;

    auto [deserialized, proto] = roundTrip<Eg::AprPolygon>(original);
    ASSERT_NE(deserialized, nullptr);
    EXPECT_EQ(deserialized->eType, Eg::EType::POLYGON);

    auto* poly = static_cast<Eg::AprPolygon*>(deserialized.get());
    EXPECT_EQ(poly->nSides, 6);
    EXPECT_DOUBLE_EQ(poly->dCircumRadius, 30.0);
    // 参数化多边形顶点由 computeVertices 计算，验证顶点数
    const auto& resultVerts = poly->vertices();
    EXPECT_EQ(resultVerts.size(), 6u);
}

// ---- 三次贝塞尔序列化闭环 ----

TEST(AprEntitySerializerTest, BezierRoundTrip)
{
    Eg::AprBezier original;
    original.id = 15;
    original.basePoint = Ut::Vec2d(0.0, 0.0);
    original.ptCtrl0 = Ut::Vec2d(50.0, 100.0);
    original.ptCtrl1 = Ut::Vec2d(100.0, 100.0);
    original.ptEnd = Ut::Vec2d(150.0, 0.0);

    auto [deserialized, proto] = roundTrip<Eg::AprBezier>(original);
    ASSERT_NE(deserialized, nullptr);
    EXPECT_EQ(deserialized->eType, Eg::EType::BEZIER);

    auto* bz = static_cast<Eg::AprBezier*>(deserialized.get());
    expectVec2dEqual(bz->ptCtrl0, Ut::Vec2d(50.0, 100.0));
    expectVec2dEqual(bz->ptCtrl1, Ut::Vec2d(100.0, 100.0));
    expectVec2dEqual(bz->ptEnd, Ut::Vec2d(150.0, 0.0));
}

// ---- 二次贝塞尔序列化闭环 ----

TEST(AprEntitySerializerTest, Bezier2RoundTrip)
{
    Eg::AprBezier2 original;
    original.id = 16;
    original.basePoint = Ut::Vec2d(0.0, 0.0);
    original.ptCtrl = Ut::Vec2d(50.0, 100.0);
    original.ptEnd = Ut::Vec2d(100.0, 0.0);

    auto [deserialized, proto] = roundTrip<Eg::AprBezier2>(original);
    ASSERT_NE(deserialized, nullptr);
    EXPECT_EQ(deserialized->eType, Eg::EType::BEZIER2);

    auto* bz = static_cast<Eg::AprBezier2*>(deserialized.get());
    expectVec2dEqual(bz->ptCtrl, Ut::Vec2d(50.0, 100.0));
    expectVec2dEqual(bz->ptEnd, Ut::Vec2d(100.0, 0.0));
}

// ---- 文本序列化闭环 ----

TEST(AprEntitySerializerTest, TextRoundTrip)
{
    Eg::AprText original;
    original.id = 20;
    original.basePoint = Ut::Vec2d(100.0, 200.0);
    original.setText("Hello Protobuf");
    original.setFontName("Arial");
    original.dHeight = 14.0;
    original.dRotation = 0.0;
    original.hAlign = Eg::AprTextHAlign::Center;
    original.vAlign = Eg::AprTextVAlign::Middle;
    original.bBold = true;
    original.bItalic = false;

    auto [deserialized, proto] = roundTrip<Eg::AprText>(original);
    ASSERT_NE(deserialized, nullptr);
    EXPECT_EQ(deserialized->eType, Eg::EType::TEXT);

    auto* txt = static_cast<Eg::AprText*>(deserialized.get());
    EXPECT_EQ(txt->textStr(), "Hello Protobuf");
    EXPECT_EQ(txt->fontNameStr(), "Arial");
    EXPECT_DOUBLE_EQ(txt->dHeight, 14.0);
    EXPECT_DOUBLE_EQ(txt->dRotation, 0.0);
    EXPECT_EQ(txt->hAlign, Eg::AprTextHAlign::Center);
    EXPECT_EQ(txt->vAlign, Eg::AprTextVAlign::Middle);
    EXPECT_TRUE(txt->bBold);
    EXPECT_FALSE(txt->bItalic);
}

// ---- 条形码序列化闭环 ----

TEST(AprEntitySerializerTest, BarCodeRoundTrip)
{
    Eg::AprBarCode original;
    original.id = 25;
    original.basePoint = Ut::Vec2d(10.0, 20.0);
    original.setData("1234567890");
    original.dWidth = 50.0;
    original.dHeight = 30.0;

    auto [deserialized, proto] = roundTrip<Eg::AprBarCode>(original);
    ASSERT_NE(deserialized, nullptr);
    EXPECT_EQ(deserialized->eType, Eg::EType::BAR_CODE);

    auto* bc = static_cast<Eg::AprBarCode*>(deserialized.get());
    EXPECT_EQ(bc->dataStr(), "1234567890");
    EXPECT_DOUBLE_EQ(bc->dWidth, 50.0);
    EXPECT_DOUBLE_EQ(bc->dHeight, 30.0);
}

// ---- 二维码序列化闭环 ----

TEST(AprEntitySerializerTest, QRCodeRoundTrip)
{
    Eg::AprQRCode original;
    original.id = 26;
    original.basePoint = Ut::Vec2d(5.0, 5.0);
    original.setData("QR_CONTENT");
    original.dModuleSize = 10.0;
    original.setEcc(Eg::QrEcc::High);
    original.setBorderSize(2);

    auto [deserialized, proto] = roundTrip<Eg::AprQRCode>(original);
    ASSERT_NE(deserialized, nullptr);
    EXPECT_EQ(deserialized->eType, Eg::EType::QR_CODE);

    auto* qr = static_cast<Eg::AprQRCode*>(deserialized.get());
    EXPECT_EQ(qr->dataStr(), "QR_CONTENT");
    EXPECT_DOUBLE_EQ(qr->dModuleSize, 10.0);
    EXPECT_EQ(qr->ecc(), Eg::QrEcc::High);
    EXPECT_EQ(qr->borderSize(), 2);
}

// 旧文件（无 ecc/border 字段）反序列化应回落默认值：M / 4
TEST(AprEntitySerializerTest, QRCodeLegacyDefaults)
{
    Eg::AprQRCode original;
    original.id = 27;
    original.setData("LEGACY");
    original.dModuleSize = 3.0;

    auto [deserialized, proto] = roundTrip<Eg::AprQRCode>(original);
    ASSERT_NE(deserialized, nullptr);

    // 模拟旧版本文件：清除 ecc/border 字段后重新反序列化
    ASSERT_TRUE(proto.has_qrcode_data());
    proto.mutable_qrcode_data()->clear_ecc();
    proto.mutable_qrcode_data()->clear_border_size();
    auto legacy = Fio::AprEntitySerializer::deserializeEntity(proto);
    ASSERT_NE(legacy, nullptr);

    auto* qr = static_cast<Eg::AprQRCode*>(legacy.get());
    EXPECT_EQ(qr->dataStr(), "LEGACY");
    EXPECT_DOUBLE_EQ(qr->dModuleSize, 3.0);
    EXPECT_EQ(qr->ecc(), Eg::QrEcc::Medium);
    EXPECT_EQ(qr->borderSize(), 4);
}

// ---- NURBS 序列化闭环 ----

TEST(AprEntitySerializerTest, SplineRoundTrip)
{
    Eg::AprNurbs original;
    original.id = 30;
    original.basePoint = Ut::Vec2d(0.0, 0.0);
    original.nDegree = 3;
    original.bClosed = false;
    original.addControlPoint(Ut::Vec2d(0.0, 0.0));
    original.addControlPoint(Ut::Vec2d(5.0, 10.0));
    original.addControlPoint(Ut::Vec2d(10.0, 0.0));
    original.addKnot(0.0);
    original.addKnot(0.0);
    original.addKnot(0.0);
    original.addKnot(0.5);
    original.addKnot(1.0);
    original.addWeight(1.0);
    original.addWeight(1.0);
    original.addWeight(1.0);

    auto [deserialized, proto] = roundTrip<Eg::AprNurbs>(original);
    ASSERT_NE(deserialized, nullptr);
    EXPECT_EQ(deserialized->eType, Eg::EType::SPLINE);

    auto* spl = static_cast<Eg::AprNurbs*>(deserialized.get());
    EXPECT_EQ(spl->nDegree, 3);
    EXPECT_EQ(spl->controlPointCount(), 3u);
    EXPECT_EQ(spl->knotCount(), 5u);
    EXPECT_EQ(spl->weightCount(), 3u);
    expectVec2dEqual(spl->controlPointAt(1), Ut::Vec2d(5.0, 10.0));
    EXPECT_DOUBLE_EQ(spl->knotAt(3), 0.5);
    EXPECT_DOUBLE_EQ(spl->weightAt(0), 1.0);
}

// ---- 图像序列化闭环 ----

TEST(AprEntitySerializerTest, ImageRoundTrip)
{
    Eg::AprImage original;
    original.id = 35;
    original.basePoint = Ut::Vec2d(0.0, 0.0);
    original.nWidth = 100;
    original.nHeight = 200;
    original.ePixelFormat = Eg::AprPixelFormat::RGBA32;
    original.setPixelDataVector({ 0xFF, 0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF });
    original.topLeft = Ut::Vec2d(0.0, 0.0);
    original.topRight = Ut::Vec2d(100.0, 0.0);
    original.bottomLeft = Ut::Vec2d(0.0, 200.0);
    original.bottomRight = Ut::Vec2d(100.0, 200.0);

    auto [deserialized, proto] = roundTrip<Eg::AprImage>(original);
    ASSERT_NE(deserialized, nullptr);
    EXPECT_EQ(deserialized->eType, Eg::EType::IMAGE);

    auto* img = static_cast<Eg::AprImage*>(deserialized.get());
    EXPECT_EQ(img->nWidth, 100);
    EXPECT_EQ(img->nHeight, 200);
    EXPECT_EQ(img->ePixelFormat, Eg::AprPixelFormat::RGBA32);
    EXPECT_EQ(img->pixelDataSize(), 8u);
    EXPECT_EQ(img->pixelData()[0], 0xFF);
    expectVec2dEqual(img->topLeft, Ut::Vec2d(0.0, 0.0));
    expectVec2dEqual(img->topRight, Ut::Vec2d(100.0, 0.0));
    expectVec2dEqual(img->bottomLeft, Ut::Vec2d(0.0, 200.0));
    expectVec2dEqual(img->bottomRight, Ut::Vec2d(100.0, 200.0));
}

// ---- 空实体序列化 ----

TEST(AprEntitySerializerTest, EmptyLineRoundTrip)
{
    // 空线段（无顶点）序列化后应有 0 个顶点
    Eg::AprLine original;
    original.id = 99;

    auto [deserialized, proto] = roundTrip<Eg::AprLine>(original);
    ASSERT_NE(deserialized, nullptr);
    EXPECT_EQ(deserialized->eType, Eg::EType::LINE);

    auto* line = static_cast<Eg::AprLine*>(deserialized.get());
    EXPECT_TRUE(line->pointRef().empty());
}

// ---- 序列化通用属性验证 ----

TEST(AprEntitySerializerTest, CommonPropertiesRoundTrip)
{
    // 验证 id, basePoint, bClosed, bCCW 等通用属性正确序列化
    Eg::AprCircle original;
    original.id = 12345;
    original.basePoint = Ut::Vec2d(-100.0, 200.0);
    original.bClosed = true;
    original.bCCW = true;
    original.dRadius = 15.0;

    auto [deserialized, proto] = roundTrip<Eg::AprCircle>(original);
    ASSERT_NE(deserialized, nullptr);
    EXPECT_EQ(deserialized->id, 12345u);
    auto* circleCommon = static_cast<Eg::AprCircle*>(deserialized.get());
    expectVec2dEqual(circleCommon->basePoint, Ut::Vec2d(-100.0, 200.0));
    EXPECT_TRUE(circleCommon->bClosed);
    EXPECT_TRUE(circleCommon->bCCW);
}

TEST(AprEntitySerializerTest, NameRoundTrip)
{
    Eg::AprCircle original;
    original.id = 88;
    original.setName("LayeredCircle");
    original.basePoint = Ut::Vec2d(1.0, 2.0);
    original.dRadius = 9.0;

    auto [deserialized, proto] = roundTrip<Eg::AprCircle>(original);
    ASSERT_NE(deserialized, nullptr);
    EXPECT_EQ(proto.name(), "LayeredCircle");
    EXPECT_STREQ(deserialized->name(), "LayeredCircle");
}

// ---- 大 ID 值序列化 ----

TEST(AprEntitySerializerTest, LargeIdRoundTrip)
{
    Eg::AprPoint original;
    original.id = 0xFFFFFFFFFFFFFFFFULL;  // 最大 uint64_t
    original.basePoint = Ut::Vec2d(0.0, 0.0);

    auto [deserialized, proto] = roundTrip<Eg::AprPoint>(original);
    ASSERT_NE(deserialized, nullptr);
    EXPECT_EQ(deserialized->id, 0xFFFFFFFFFFFFFFFFULL);
}

// ---- 顶点精度验证 ----

TEST(AprEntitySerializerTest, DoublePrecisionRoundTrip)
{
    // 验证双精度浮点数在序列化后精度不丢失
    Eg::AprCircle original;
    original.id = 1;
    original.basePoint = Ut::Vec2d(1.23456789012345, -9.87654321098765);
    original.dRadius = 3.14159265358979;

    auto [deserialized, proto] = roundTrip<Eg::AprCircle>(original);
    ASSERT_NE(deserialized, nullptr);

    auto* circle = static_cast<Eg::AprCircle*>(deserialized.get());
    EXPECT_DOUBLE_EQ(circle->basePoint.x(), 1.23456789012345);
    EXPECT_DOUBLE_EQ(circle->basePoint.y(), -9.87654321098765);
    EXPECT_DOUBLE_EQ(circle->dRadius, 3.14159265358979);
}

// ---- 中文文本序列化 ----

TEST(AprEntitySerializerTest, ChineseTextRoundTrip)
{
    Eg::AprText original;
    original.id = 40;
    original.basePoint = Ut::Vec2d(0.0, 0.0);
    original.setText("你好世界");
    original.setFontName("宋体");
    original.dHeight = 16.0;

    auto [deserialized, proto] = roundTrip<Eg::AprText>(original);
    ASSERT_NE(deserialized, nullptr);

    auto* txt = static_cast<Eg::AprText*>(deserialized.get());
    EXPECT_EQ(txt->textStr(), "你好世界");
    EXPECT_EQ(txt->fontNameStr(), "宋体");
}