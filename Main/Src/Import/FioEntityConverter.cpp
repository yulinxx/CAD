#include "FioEntityConverter.h"

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
#include "Engine3D/SyEntity/SyMeshEntity.h"

#include "Log/SyLogger.h"

// ============================================================================
// FioEntityConverter 实现
// 将 FileIO 中立 IR（POD）转换为 Engine 领域对象（SyEntity）
// 使用直接成员赋值，SyEntity 体系无 setter 方法，均为 public 成员
// ============================================================================

std::unique_ptr<Eg::SyEntity> FioEntityConverter::convertEntity(const Fio::EntityInfo& info)
{
    // 委托到带扩展数据的版本（空 blob）
    return convertEntity(info, Fio::BinaryBlob{});
}

std::unique_ptr<Eg::SyEntity> FioEntityConverter::convertEntity(
    const Fio::EntityInfo& info, const Fio::BinaryBlob& blob)
{
    switch (info.type)
    {
        case Fio::EntityType::Line:
        {
            auto e = std::make_unique<Eg::SyLine>();
            e->setName(info.name);
            e->addPoint(Ut::Vec2d(info.line.x1, info.line.y1));
            e->addPoint(Ut::Vec2d(info.line.x2, info.line.y2));
            e->basePoint = e->pointRef()[0];

            //SY_INFOF("[FioEntityConverter] Converted Line: (%.2f,%.2f)->(%.2f,%.2f)",
            //    info.line.x1, info.line.y1, info.line.x2, info.line.y2);

            return std::move(e);
        }
        case Fio::EntityType::Arc:
        {
            auto e = std::make_unique<Eg::SyArc>();
            e->setName(info.name);
            e->basePoint = Ut::Vec2d(info.arc.cx, info.arc.cy);
            e->dRadius = info.arc.r;
            e->dStartAngle = info.arc.sa;
            e->dEndAngle = info.arc.ea;
            return std::move(e);
        }
        case Fio::EntityType::Circle:
        {
            auto e = std::make_unique<Eg::SyCircle>();
            e->setName(info.name);
            e->basePoint = Ut::Vec2d(info.circle.cx, info.circle.cy);
            e->dRadius = info.circle.r;
            return std::move(e);
        }
        case Fio::EntityType::Ellipse:
        {
            auto e = std::make_unique<Eg::SyEllipse>();
            e->setName(info.name);
            e->basePoint = Ut::Vec2d(info.ellipse.cx, info.ellipse.cy);
            e->dRadiusX = info.ellipse.rx;
            e->dRadiusY = info.ellipse.ry;
            e->dRotation = info.ellipse.rot;
            e->dStartAngle = info.ellipse.sa;
            e->dEndAngle = info.ellipse.ea;
            return std::move(e);
        }
        case Fio::EntityType::Point:
        {
            auto e = std::make_unique<Eg::SyPoint>();
            e->setName(info.name);
            // 复用 line.x1/y1 作为点坐标
            e->basePoint = Ut::Vec2d(info.line.x1, info.line.y1);
            return std::move(e);
        }
        case Fio::EntityType::Bezier:
        {
            auto e = std::make_unique<Eg::SyBezier>();
            e->setName(info.name);
            // basePoint 是起点 P0，ptCtrl0/ptCtrl1 是控制点，ptEnd 是终点
            e->basePoint = Ut::Vec2d(info.line.x1, info.line.y1); // 复用 line.x1/y1 作为起点
            e->ptCtrl0 = Ut::Vec2d(info.bezier.c0x, info.bezier.c0y);
            e->ptCtrl1 = Ut::Vec2d(info.bezier.c1x, info.bezier.c1y);
            e->ptEnd = Ut::Vec2d(info.bezier.ex, info.bezier.ey);
            return std::move(e);
        }
        case Fio::EntityType::Bezier2:
        {
            auto e = std::make_unique<Eg::SyBezier2>();
            e->setName(info.name);
            // basePoint 是起点 P0，ptCtrl 是控制点，ptEnd 是终点
            e->basePoint = Ut::Vec2d(info.line.x1, info.line.y1); // 复用 line.x1/y1 作为起点
            e->ptCtrl = Ut::Vec2d(info.bezier2.cx, info.bezier2.cy);
            e->ptEnd = Ut::Vec2d(info.bezier2.ex, info.bezier2.ey);
            return std::move(e);
        }
        case Fio::EntityType::Text:
        {
            auto e = std::make_unique<Eg::SyText>();
            e->setName(info.name);
            e->basePoint = Ut::Vec2d(info.text.x, info.text.y);
            e->setText(info.text.text);
            e->dHeight = info.text.h;
            e->dRotation = info.text.a;
            return std::move(e);
        }
        case Fio::EntityType::Polygon:
        case Fio::EntityType::Polyline:
        {
            // 多边形/折线: 顶点数据在扩展数据块中（P5 收口: 复用 readExtensionPoints）
            auto e = std::make_unique<Eg::SyPolygon>();
            e->setName(info.name);
            if (info.vertexCount > 0 && info.extensionDataSize > 0)
            {
                auto pts = readExtensionPoints(info, blob);
                std::vector<Ut::Vec2d> verts;
                verts.reserve(pts.size());
                for (auto& p : pts)
                    verts.push_back(Ut::Vec2d(p.x, p.y));
                e->setVertices(std::move(verts));
            }
            e->bClosed = (info.type == Fio::EntityType::Polygon);
            //SY_INFOF("[FioEntityConverter] Converted Polygon/Polyline: %u vertices", info.vertexCount);
            return std::move(e);
        }
        case Fio::EntityType::Nurbs:
        case Fio::EntityType::Spline:
        {
            // NURBS: 扩展数据布局中 [控制点] [节点] [权重]
            auto e = std::make_unique<Eg::SyNurbs>();
            e->setName(info.name);
            e->nDegree = info.nurbsDegree;
            if (info.extensionDataSize > 0)
            {
                const auto* raw = reinterpret_cast<const double*>(
                    blob.data + info.extensionDataOffset);
                uint32_t offset = 0;

                // 控制点 (nurbsCtrlPtCount * 2 doubles)
                for (uint32_t i = 0; i < info.nurbsCtrlPtCount && offset + 1 < info.extensionDataSize / sizeof(double); ++i)
                {
                    e->addControlPoint(Ut::Vec2d(raw[offset], raw[offset + 1]));
                    offset += 2;
                }
                // 节点 (nurbsKnotCount doubles)
                for (uint32_t i = 0; i < info.nurbsKnotCount && offset < info.extensionDataSize / sizeof(double); ++i)
                {
                    e->addKnot(raw[offset++]);
                }
                // 权重 (nurbsCtrlPtCount doubles)
                for (uint32_t i = 0; i < info.nurbsCtrlPtCount && offset < info.extensionDataSize / sizeof(double); ++i)
                {
                    e->addWeight(raw[offset++]);
                }
            }

            //SY_INFOF("[FioEntityConverter] Converted NURBS: degree=%d, ctrlPts=%u, knots=%u",
            //    e->nDegree, static_cast<uint32_t>(e->controlPointCount()),
            //    static_cast<uint32_t>(e->knotCount()));

            return std::move(e);
        }
        case Fio::EntityType::Image:
        {
            // 图像: 像素数据在扩展数据块中
            auto e = std::make_unique<Eg::SyImage>();
            e->setName(info.name);
            e->nWidth = info.imageWidth;
            e->nHeight = info.imageHeight;
            if (info.extensionDataSize > 0)
            {
                // 图像像素数据从扩展数据块中读取
                e->setPixelData(
                    reinterpret_cast<const unsigned char*>(blob.data + info.extensionDataOffset),
                    info.extensionDataSize);
            }
            e->topLeft = Ut::Vec2d(info.line.x1, info.line.y1);
            // 补全其余三角：以 topLeft 为锚点，按像素宽高建立世界坐标四边形
            // （无 dpi 信息，采用 1 像素 = 1 世界单位的近似；Y 轴向上为正）
            const double w = static_cast<double>(info.imageWidth);
            const double h = static_cast<double>(info.imageHeight);
            e->topRight = Ut::Vec2d(info.line.x1 + w, info.line.y1);
            e->bottomLeft = Ut::Vec2d(info.line.x1, info.line.y1 - h);
            e->bottomRight = Ut::Vec2d(info.line.x1 + w, info.line.y1 - h);
            //SY_INFOF("[FioEntityConverter] Converted Image: %dx%d", e->nWidth, e->nHeight);
            return std::move(e);
        }
        case Fio::EntityType::BarCode:
        {
            auto e = std::make_unique<Eg::SyBarCode>();
            e->setName(info.name);
            e->setData(info.text.text);
            e->dWidth = info.barWidth;
            e->dHeight = info.barHeight;
            e->basePoint = Ut::Vec2d(info.text.x, info.text.y);
            //SY_INFOF("[FioEntityConverter] Converted BarCode: %s", e->data());
            return std::move(e);
        }
        case Fio::EntityType::QRCode:
        {
            auto e = std::make_unique<Eg::SyQRCode>();
            e->setName(info.name);
            e->setData(info.text.text);
            e->dModuleSize = info.moduleSize;
            e->basePoint = Ut::Vec2d(info.text.x, info.text.y);
            //SY_INFOF("[FioEntityConverter] Converted QRCode: %s", e->data());
            return std::move(e);
        }
        case Fio::EntityType::Mesh3D:
        {
            auto e = std::make_unique<Eg::SyMeshEntity>();
            e->setName(info.name);

            if (info.meshVertCount > 0 && info.extensionDataSize > 0 && blob.data)
            {
                const float* raw = reinterpret_cast<const float*>(
                    blob.data + info.extensionDataOffset);

                uint32_t vertCount = info.meshVertCount;
                uint32_t normalOffset = vertCount * 3;
                size_t totalFloats = info.extensionDataSize / sizeof(float);
                e->vertices.reserve(vertCount);
                e->normals.reserve(vertCount);

                for (uint32_t i = 0; i < vertCount; ++i)
                {
                    e->vertices.emplace_back(raw[i * 3], raw[i * 3 + 1], raw[i * 3 + 2]);
                }

                for (uint32_t i = 0; i < vertCount && (normalOffset + i * 3 + 2) < totalFloats; ++i)
                {
                    e->normals.emplace_back(raw[normalOffset + i * 3],
                        raw[normalOffset + i * 3 + 1],
                        raw[normalOffset + i * 3 + 2]);
                }

                while (e->normals.size() < vertCount)
                {
                    e->normals.emplace_back(0.0f, 0.0f, 1.0f);
                }

                e->markDirty();
            }
            return std::move(e);
        }
        default:
        {
            SY_ERRORF("[FioEntityConverter] Unknown entity type: %d", static_cast<int>(info.type));
        }
        return nullptr;
    }
}

std::vector<std::unique_ptr<Eg::SyEntity>> FioEntityConverter::convertAll(const Fio::FioParseResult& parseData)
{
    std::vector<std::unique_ptr<Eg::SyEntity>> result;
    result.reserve(parseData.entityCount);

    //SY_INFOF("[FioEntityConverter] Converting %u entities from ParseData (format=%s)",
        //parseData.entityCount, parseData.sourceFormat);

    for (uint32_t i = 0; i < parseData.entityCount; ++i)
    {
        auto entity = convertEntity(parseData.entities[i], parseData.extensionBlob);
        if (entity)
        {
            // 设置公共属性（SyEntity 基类提供的 setter）
            entity->setVisible(parseData.entities[i].visible);
            entity->setLocked(parseData.entities[i].locked);
            // 注意：SyEntity 体系无 lineWidth 成员，线宽由渲染层控制
            result.push_back(std::move(entity));
        }
    }

    //SY_INFOF("[FioEntityConverter] Converted %zu entities successfully", result.size());
    return result;
}

std::vector<Fio::IrLayerInfo> FioEntityConverter::extractLayers(const Fio::FioParseResult& parseData)
{
    std::vector<Fio::IrLayerInfo> layers;
    layers.reserve(parseData.layerCount);
    for (uint32_t i = 0; i < parseData.layerCount; ++i)
    {
        layers.push_back(parseData.layers[i]);
    }
    return layers;
}

std::vector<Fio::Point2D> FioEntityConverter::readExtensionPoints(
    const Fio::EntityInfo& info,
    const Fio::BinaryBlob& blob)
{
    std::vector<Fio::Point2D> points;
    if (info.extensionDataSize == 0 || !blob.data)
        return points;

    const auto* raw = reinterpret_cast<const double*>(
        blob.data + info.extensionDataOffset);
    const uint32_t count = info.extensionDataSize / sizeof(Fio::Point2D);

    points.reserve(count);
    for (uint32_t i = 0; i < count; ++i)
    {
        Fio::Point2D pt;
        pt.x = raw[i * 2];
        pt.y = raw[i * 2 + 1];
        points.push_back(pt);
    }
    return points;
}