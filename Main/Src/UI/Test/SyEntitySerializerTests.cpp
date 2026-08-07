/**
 * @file SyEntitySerializerTests.cpp
 * @brief SyEntitySerializer 鍥炲綊娴嬭瘯 鈥?SyEntity 鈫?Protobuf 搴忓垪鍖栭棴鐜? *
 * 瑕嗙洊锛? *   - 鎵€鏈夊疄浣撶被鍨嬬殑 serializeEntity 鈫?deserializeEntity 闂幆
 *   - 閫氱敤灞炴€э紙id, basePoint, bClosed, bCCW锛夌殑搴忓垪鍖? *   - 鍥惧眰鍏宠仈鐨勫簭鍒楀寲
 *   - 澶嶆潅瀹炰綋绫诲瀷锛圢URBS, Image锛夌殑搴忓垪鍖? *
 * P2 娴嬭瘯瑕嗙洊鎵╁睍 (2026-07-30)
 */

#include <gtest/gtest.h>

#include "SyEntitySerializer.h"
#include "FileIO/SyDocument.h"

 // Engine2D 瀹炰綋绫诲瀷
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

#include "SanYiDocument.pb.h"

namespace
{
    /// 杈呭姪锛氬皢瀹炰綋搴忓垪鍖栧悗绔嬪嵆鍙嶅簭鍒楀寲锛岄獙璇侀棴鐜?
    template<typename T>
    std::pair<std::unique_ptr<Eg::SyEntity>, sanyi::proto::EntityData>
        roundTrip(const Eg::SyEntity& entity)
    {
        sanyi::proto::EntityData protoData;
        Fio::SyEntitySerializer::serializeEntity(entity, &protoData);

        auto deserialized = Fio::SyEntitySerializer::deserializeEntity(protoData);
        return { std::move(deserialized), std::move(protoData) };
    }

    /// 杈呭姪锛氶獙璇佷袱涓?Vec2d 鐩哥瓑
    void expectVec2dEqual(const Ut::Vec2d& a, const Ut::Vec2d& b)
    {
        EXPECT_DOUBLE_EQ(a.x(), b.x());
        EXPECT_DOUBLE_EQ(a.y(), b.y());
    }
}

// ==================== 绾挎搴忓垪鍖栭棴鐜?====================

TEST(SyEntitySerializerTest, LineRoundTrip)
{
    Eg::SyLine original;
    original.id = 42;
    original.basePoint = Ut::Vec2d(10.0, 20.0);
    original.bClosed = false;
    original.bCCW = true;
    original.addPoint(Ut::Vec2d(10.0, 20.0));
    original.addPoint(Ut::Vec2d(100.0, 200.0));

    auto [deserialized, proto] = roundTrip<Eg::SyLine>(original);
    ASSERT_NE(deserialized, nullptr);
    EXPECT_EQ(deserialized->eType, Eg::EType::LINE);
    EXPECT_EQ(deserialized->id, 42u);
    expectVec2dEqual(deserialized->basePoint, Ut::Vec2d(10.0, 20.0));
    EXPECT_FALSE(deserialized->bClosed);
    EXPECT_TRUE(deserialized->bCCW);

    auto* line = static_cast<Eg::SyLine*>(deserialized.get());
    ASSERT_EQ(line->pointRef().size(), 2u);
    expectVec2dEqual(line->pointRef()[0], Ut::Vec2d(10.0, 20.0));
    expectVec2dEqual(line->pointRef()[1], Ut::Vec2d(100.0, 200.0));
}

// ==================== 鍦嗗簭鍒楀寲闂幆 ====================

TEST(SyEntitySerializerTest, CircleRoundTrip)
{
    Eg::SyCircle original;
    original.id = 7;
    original.basePoint = Ut::Vec2d(50.0, 60.0);
    original.dRadius = 25.0;

    auto [deserialized, proto] = roundTrip<Eg::SyCircle>(original);
    ASSERT_NE(deserialized, nullptr);
    EXPECT_EQ(deserialized->eType, Eg::EType::CIRCLE);
    EXPECT_EQ(deserialized->id, 7u);

    auto* circle = static_cast<Eg::SyCircle*>(deserialized.get());
    expectVec2dEqual(circle->basePoint, Ut::Vec2d(50.0, 60.0));
    EXPECT_DOUBLE_EQ(circle->dRadius, 25.0);
}

// ==================== 鍦嗗姬搴忓垪鍖栭棴鐜?====================

TEST(SyEntitySerializerTest, ArcRoundTrip)
{
    Eg::SyArc original;
    original.id = 3;
    original.basePoint = Ut::Vec2d(10.0, 20.0);
    original.dRadius = 30.0;
    original.dStartAngle = 0.0;
    original.dEndAngle = 1.5708;

    auto [deserialized, proto] = roundTrip<Eg::SyArc>(original);
    ASSERT_NE(deserialized, nullptr);
    EXPECT_EQ(deserialized->eType, Eg::EType::ARC);

    auto* arc = static_cast<Eg::SyArc*>(deserialized.get());
    EXPECT_DOUBLE_EQ(arc->dRadius, 30.0);
    EXPECT_DOUBLE_EQ(arc->dStartAngle, 0.0);
    EXPECT_DOUBLE_EQ(arc->dEndAngle, 1.5708);
}

// ==================== 妞渾搴忓垪鍖栭棴鐜?====================

TEST(SyEntitySerializerTest, EllipseRoundTrip)
{
    Eg::SyEllipse original;
    original.id = 5;
    original.basePoint = Ut::Vec2d(0.0, 0.0);
    original.dRadiusX = 50.0;
    original.dRadiusY = 30.0;
    original.dRotation = 0.5;

    auto [deserialized, proto] = roundTrip<Eg::SyEllipse>(original);
    ASSERT_NE(deserialized, nullptr);
    EXPECT_EQ(deserialized->eType, Eg::EType::ELLIPSE);

    auto* ell = static_cast<Eg::SyEllipse*>(deserialized.get());
    EXPECT_DOUBLE_EQ(ell->dRadiusX, 50.0);
    EXPECT_DOUBLE_EQ(ell->dRadiusY, 30.0);
    EXPECT_DOUBLE_EQ(ell->dRotation, 0.5);
}

// ==================== 鐐瑰簭鍒楀寲闂幆 ====================

TEST(SyEntitySerializerTest, PointRoundTrip)
{
    Eg::SyPoint original;
    original.id = 1;
    original.basePoint = Ut::Vec2d(42.0, 99.0);

    auto [deserialized, proto] = roundTrip<Eg::SyPoint>(original);
    ASSERT_NE(deserialized, nullptr);
    EXPECT_EQ(deserialized->eType, Eg::EType::POINT);
    expectVec2dEqual(deserialized->basePoint, Ut::Vec2d(42.0, 99.0));
}

// ==================== 澶氳竟褰㈠簭鍒楀寲闂幆锛堟樉寮忛《鐐癸級 ====================

TEST(SyEntitySerializerTest, PolygonRoundTrip)
{
    // 浣跨敤 setVertices 璁剧疆鏄惧紡椤剁偣锛岄伩鍏嶄笌鍙傛暟鍖栧畾涔夌殑鍐茬獊
    Eg::SyPolygon original;
    original.id = 10;
    original.basePoint = Ut::Vec2d(0.0, 0.0);
    original.bClosed = true;
    original.bCCW = false;

    original.setVertices({
        Ut::Vec2d(0.0, 0.0),
        Ut::Vec2d(10.0, 0.0),
        Ut::Vec2d(10.0, 10.0),
        Ut::Vec2d(0.0, 10.0)
        });

    auto [deserialized, proto] = roundTrip<Eg::SyPolygon>(original);
    ASSERT_NE(deserialized, nullptr);
    EXPECT_EQ(deserialized->eType, Eg::EType::POLYGON);
    EXPECT_TRUE(deserialized->bClosed);
    EXPECT_FALSE(deserialized->bCCW);

    auto* poly = static_cast<Eg::SyPolygon*>(deserialized.get());
    const auto& resultVerts = poly->vertices();
    ASSERT_EQ(resultVerts.size(), 4u);
    expectVec2dEqual(resultVerts[0], Ut::Vec2d(0.0, 0.0));
    expectVec2dEqual(resultVerts[3], Ut::Vec2d(0.0, 10.0));
}

// ==================== 澶氳竟褰㈠簭鍒楀寲闂幆锛堝弬鏁板寲锛?====================

TEST(SyEntitySerializerTest, ParametricPolygonRoundTrip)
{
    // 浠呬娇鐢ㄥ弬鏁板寲瀹氫箟锛坣Sides + dCircumRadius锛夛紝涓嶈缃樉寮忛《鐐?
    Eg::SyPolygon original;
    original.id = 11;
    original.basePoint = Ut::Vec2d(50.0, 50.0);
    original.nSides = 6;
    original.dCircumRadius = 30.0;

    auto [deserialized, proto] = roundTrip<Eg::SyPolygon>(original);
    ASSERT_NE(deserialized, nullptr);
    EXPECT_EQ(deserialized->eType, Eg::EType::POLYGON);

    auto* poly = static_cast<Eg::SyPolygon*>(deserialized.get());
    EXPECT_EQ(poly->nSides, 6);
    EXPECT_DOUBLE_EQ(poly->dCircumRadius, 30.0);
    // 鍙傛暟鍖栧杈瑰舰椤剁偣鐢?computeVertices 璁＄畻锛岄獙璇侀《鐐规暟
    const auto& resultVerts = poly->vertices();
    EXPECT_EQ(resultVerts.size(), 6u);
}

// ==================== 涓夋璐濆灏斿簭鍒楀寲闂幆 ====================

TEST(SyEntitySerializerTest, BezierRoundTrip)
{
    Eg::SyBezier original;
    original.id = 15;
    original.basePoint = Ut::Vec2d(0.0, 0.0);
    original.ptCtrl0 = Ut::Vec2d(50.0, 100.0);
    original.ptCtrl1 = Ut::Vec2d(100.0, 100.0);
    original.ptEnd = Ut::Vec2d(150.0, 0.0);

    auto [deserialized, proto] = roundTrip<Eg::SyBezier>(original);
    ASSERT_NE(deserialized, nullptr);
    EXPECT_EQ(deserialized->eType, Eg::EType::BEZIER);

    auto* bz = static_cast<Eg::SyBezier*>(deserialized.get());
    expectVec2dEqual(bz->ptCtrl0, Ut::Vec2d(50.0, 100.0));
    expectVec2dEqual(bz->ptCtrl1, Ut::Vec2d(100.0, 100.0));
    expectVec2dEqual(bz->ptEnd, Ut::Vec2d(150.0, 0.0));
}

// ==================== 浜屾璐濆灏斿簭鍒楀寲闂幆 ====================

TEST(SyEntitySerializerTest, Bezier2RoundTrip)
{
    Eg::SyBezier2 original;
    original.id = 16;
    original.basePoint = Ut::Vec2d(0.0, 0.0);
    original.ptCtrl = Ut::Vec2d(50.0, 100.0);
    original.ptEnd = Ut::Vec2d(100.0, 0.0);

    auto [deserialized, proto] = roundTrip<Eg::SyBezier2>(original);
    ASSERT_NE(deserialized, nullptr);
    EXPECT_EQ(deserialized->eType, Eg::EType::BEZIER2);

    auto* bz = static_cast<Eg::SyBezier2*>(deserialized.get());
    expectVec2dEqual(bz->ptCtrl, Ut::Vec2d(50.0, 100.0));
    expectVec2dEqual(bz->ptEnd, Ut::Vec2d(100.0, 0.0));
}

// ==================== 鏂囨湰搴忓垪鍖栭棴鐜?====================

TEST(SyEntitySerializerTest, TextRoundTrip)
{
    Eg::SyText original;
    original.id = 20;
    original.basePoint = Ut::Vec2d(100.0, 200.0);
    original.setText("Hello Protobuf");
    original.setFontName("Arial");
    original.dHeight = 14.0;
    original.dRotation = 0.0;
    original.hAlign = Eg::SyTextHAlign::Center;
    original.vAlign = Eg::SyTextVAlign::Middle;
    original.bBold = true;
    original.bItalic = false;

    auto [deserialized, proto] = roundTrip<Eg::SyText>(original);
    ASSERT_NE(deserialized, nullptr);
    EXPECT_EQ(deserialized->eType, Eg::EType::TEXT);

    auto* txt = static_cast<Eg::SyText*>(deserialized.get());
    EXPECT_EQ(txt->textStr(), "Hello Protobuf");
    EXPECT_EQ(txt->fontNameStr(), "Arial");
    EXPECT_DOUBLE_EQ(txt->dHeight, 14.0);
    EXPECT_DOUBLE_EQ(txt->dRotation, 0.0);
    EXPECT_EQ(txt->hAlign, Eg::SyTextHAlign::Center);
    EXPECT_EQ(txt->vAlign, Eg::SyTextVAlign::Middle);
    EXPECT_TRUE(txt->bBold);
    EXPECT_FALSE(txt->bItalic);
}

// ==================== 鏉″舰鐮佸簭鍒楀寲闂幆 ====================

TEST(SyEntitySerializerTest, BarCodeRoundTrip)
{
    Eg::SyBarCode original;
    original.id = 25;
    original.basePoint = Ut::Vec2d(10.0, 20.0);
    original.setData("1234567890");
    original.dWidth = 50.0;
    original.dHeight = 30.0;

    auto [deserialized, proto] = roundTrip<Eg::SyBarCode>(original);
    ASSERT_NE(deserialized, nullptr);
    EXPECT_EQ(deserialized->eType, Eg::EType::BAR_CODE);

    auto* bc = static_cast<Eg::SyBarCode*>(deserialized.get());
    EXPECT_EQ(bc->dataStr(), "1234567890");
    EXPECT_DOUBLE_EQ(bc->dWidth, 50.0);
    EXPECT_DOUBLE_EQ(bc->dHeight, 30.0);
}

// ==================== 浜岀淮鐮佸簭鍒楀寲闂幆 ====================

TEST(SyEntitySerializerTest, QRCodeRoundTrip)
{
    Eg::SyQRCode original;
    original.id = 26;
    original.basePoint = Ut::Vec2d(5.0, 5.0);
    original.setData("QR_CONTENT");
    original.dModuleSize = 10.0;

    auto [deserialized, proto] = roundTrip<Eg::SyQRCode>(original);
    ASSERT_NE(deserialized, nullptr);
    EXPECT_EQ(deserialized->eType, Eg::EType::QR_CODE);

    auto* qr = static_cast<Eg::SyQRCode*>(deserialized.get());
    EXPECT_EQ(qr->dataStr(), "QR_CONTENT");
    EXPECT_DOUBLE_EQ(qr->dModuleSize, 10.0);
}

// ==================== NURBS 搴忓垪鍖栭棴鐜?====================

TEST(SyEntitySerializerTest, SplineRoundTrip)
{
    Eg::SyNurbs original;
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

    auto [deserialized, proto] = roundTrip<Eg::SyNurbs>(original);
    ASSERT_NE(deserialized, nullptr);
    EXPECT_EQ(deserialized->eType, Eg::EType::SPLINE);

    auto* spl = static_cast<Eg::SyNurbs*>(deserialized.get());
    EXPECT_EQ(spl->nDegree, 3);
    EXPECT_EQ(spl->controlPointCount(), 3u);
    EXPECT_EQ(spl->knotCount(), 5u);
    EXPECT_EQ(spl->weightCount(), 3u);
    expectVec2dEqual(spl->controlPointAt(1), Ut::Vec2d(5.0, 10.0));
    EXPECT_DOUBLE_EQ(spl->knotAt(3), 0.5);
    EXPECT_DOUBLE_EQ(spl->weightAt(0), 1.0);
}

// ==================== 鍥惧儚搴忓垪鍖栭棴鐜?====================

TEST(SyEntitySerializerTest, ImageRoundTrip)
{
    Eg::SyImage original;
    original.id = 35;
    original.basePoint = Ut::Vec2d(0.0, 0.0);
    original.nWidth = 100;
    original.nHeight = 200;
    original.ePixelFormat = Eg::SyPixelFormat::RGBA32;
    original.setPixelDataVector({ 0xFF, 0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF });
    original.topLeft = Ut::Vec2d(0.0, 0.0);
    original.topRight = Ut::Vec2d(100.0, 0.0);
    original.bottomLeft = Ut::Vec2d(0.0, 200.0);
    original.bottomRight = Ut::Vec2d(100.0, 200.0);

    auto [deserialized, proto] = roundTrip<Eg::SyImage>(original);
    ASSERT_NE(deserialized, nullptr);
    EXPECT_EQ(deserialized->eType, Eg::EType::IMAGE);

    auto* img = static_cast<Eg::SyImage*>(deserialized.get());
    EXPECT_EQ(img->nWidth, 100);
    EXPECT_EQ(img->nHeight, 200);
    EXPECT_EQ(img->ePixelFormat, Eg::SyPixelFormat::RGBA32);
    EXPECT_EQ(img->pixelDataSize(), 8u);
    EXPECT_EQ(img->pixelData()[0], 0xFF);
    expectVec2dEqual(img->topLeft, Ut::Vec2d(0.0, 0.0));
    expectVec2dEqual(img->topRight, Ut::Vec2d(100.0, 0.0));
    expectVec2dEqual(img->bottomLeft, Ut::Vec2d(0.0, 200.0));
    expectVec2dEqual(img->bottomRight, Ut::Vec2d(100.0, 200.0));
}

// ==================== 绌哄疄浣撳簭鍒楀寲 ====================

TEST(SyEntitySerializerTest, EmptyLineRoundTrip)
{
    // 绌虹嚎娈碉紙鏃犻《鐐癸級搴忓垪鍖栧悗搴旀湁 0 涓《鐐?
    Eg::SyLine original;
    original.id = 99;

    auto [deserialized, proto] = roundTrip<Eg::SyLine>(original);
    ASSERT_NE(deserialized, nullptr);
    EXPECT_EQ(deserialized->eType, Eg::EType::LINE);

    auto* line = static_cast<Eg::SyLine*>(deserialized.get());
    EXPECT_TRUE(line->pointRef().empty());
}

// ==================== 搴忓垪鍖栭€氱敤灞炴€ч獙璇?====================

TEST(SyEntitySerializerTest, CommonPropertiesRoundTrip)
{
    // 楠岃瘉 id, basePoint, bClosed, bCCW 绛夐€氱敤灞炴€ф纭簭鍒楀寲
    Eg::SyCircle original;
    original.id = 12345;
    original.basePoint = Ut::Vec2d(-100.0, 200.0);
    original.bClosed = true;
    original.bCCW = true;
    original.dRadius = 15.0;

    auto [deserialized, proto] = roundTrip<Eg::SyCircle>(original);
    ASSERT_NE(deserialized, nullptr);
    EXPECT_EQ(deserialized->id, 12345u);
    expectVec2dEqual(deserialized->basePoint, Ut::Vec2d(-100.0, 200.0));
    EXPECT_TRUE(deserialized->bClosed);
    EXPECT_TRUE(deserialized->bCCW);
}

TEST(SyEntitySerializerTest, NameRoundTrip)
{
    Eg::SyCircle original;
    original.id = 88;
    original.setName("LayeredCircle");
    original.basePoint = Ut::Vec2d(1.0, 2.0);
    original.dRadius = 9.0;

    auto [deserialized, proto] = roundTrip<Eg::SyCircle>(original);
    ASSERT_NE(deserialized, nullptr);
    EXPECT_EQ(proto.name(), "LayeredCircle");
    EXPECT_STREQ(deserialized->name(), "LayeredCircle");
}

// ==================== 澶?ID 鍊煎簭鍒楀寲 ====================

TEST(SyEntitySerializerTest, LargeIdRoundTrip)
{
    Eg::SyPoint original;
    original.id = 0xFFFFFFFFFFFFFFFFULL;  // 鏈€澶?uint64_t
    original.basePoint = Ut::Vec2d(0.0, 0.0);

    auto [deserialized, proto] = roundTrip<Eg::SyPoint>(original);
    ASSERT_NE(deserialized, nullptr);
    EXPECT_EQ(deserialized->id, 0xFFFFFFFFFFFFFFFFULL);
}

// ==================== 娴偣绮惧害楠岃瘉 ====================

TEST(SyEntitySerializerTest, DoublePrecisionRoundTrip)
{
    // 楠岃瘉鍙岀簿搴︽诞鐐规暟鍦ㄥ簭鍒楀寲鍚庣簿搴︿笉涓㈠け
    Eg::SyCircle original;
    original.id = 1;
    original.basePoint = Ut::Vec2d(1.23456789012345, -9.87654321098765);
    original.dRadius = 3.14159265358979;

    auto [deserialized, proto] = roundTrip<Eg::SyCircle>(original);
    ASSERT_NE(deserialized, nullptr);

    auto* circle = static_cast<Eg::SyCircle*>(deserialized.get());
    EXPECT_DOUBLE_EQ(circle->basePoint.x(), 1.23456789012345);
    EXPECT_DOUBLE_EQ(circle->basePoint.y(), -9.87654321098765);
    EXPECT_DOUBLE_EQ(circle->dRadius, 3.14159265358979);
}

// ==================== 涓枃鏂囨湰搴忓垪鍖?====================

TEST(SyEntitySerializerTest, ChineseTextRoundTrip)
{
    Eg::SyText original;
    original.id = 40;
    original.basePoint = Ut::Vec2d(0.0, 0.0);
    original.setText("浣犲ソ涓栫晫");
    original.setFontName("瀹嬩綋");
    original.dHeight = 16.0;

    auto [deserialized, proto] = roundTrip<Eg::SyText>(original);
    ASSERT_NE(deserialized, nullptr);

    auto* txt = static_cast<Eg::SyText*>(deserialized.get());
    EXPECT_EQ(txt->textStr(), "浣犲ソ涓栫晫");
    EXPECT_EQ(txt->fontNameStr(), "瀹嬩綋");
}