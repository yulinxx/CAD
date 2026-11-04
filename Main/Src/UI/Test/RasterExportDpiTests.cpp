/**
 * @file RasterExportDpiTests.cpp
 * @brief 位图导出物理单位测试
 *
 * 世界坐标单位为毫米，导出必须按 context.dpi 换算像素；
 * PNG 还需要写 pHYs，让下游软件读到的 DPI 与导出设置一致。
 */

#include "Export/Writers/RasterExportHelper.h"
#include "Export/ExportContext.h"

#include "Engine2D/SyEntity/SyLine.h"
#include "Engine2D/SyEntity/SyPoint.h"
#include "Engine2D/SyEntity/SyCircle.h"
#include "Engine2D/SyEntity/SyArc.h"
#include "Engine2D/SyEntity/SyPolygon.h"
#include "Engine2D/SyEntity/SyFillRegion.h"

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

namespace
{
    /** @brief 从 PNG 文件读取 pHYs，返回换算后的 DPI（1 英寸 = 0.0254 米） */
    int readPngDpi(const QString& path)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return -1;
        const QByteArray data = file.readAll();
        file.close();

        if (data.size() < 8 || std::memcmp(data.constData(), "\x89PNG\r\n\x1a\n", 8) != 0) return -1;

        int pos = 8;
        while (pos + 12 <= data.size())
        {
            const uint32_t length =
                (static_cast<uint32_t>(static_cast<uint8_t>(data[pos])) << 24)
                | (static_cast<uint32_t>(static_cast<uint8_t>(data[pos + 1])) << 16)
                | (static_cast<uint32_t>(static_cast<uint8_t>(data[pos + 2])) << 8)
                | static_cast<uint32_t>(static_cast<uint8_t>(data[pos + 3]));
            const QByteArray type = data.mid(pos + 4, 4);
            if (type == QByteArrayLiteral("pHYs") && length == 9)
            {
                const uint8_t* payload =
                    reinterpret_cast<const uint8_t*>(data.constData()) + pos + 8;
                const uint32_t ppmX =
                    (static_cast<uint32_t>(payload[0]) << 24)
                    | (static_cast<uint32_t>(payload[1]) << 16)
                    | (static_cast<uint32_t>(payload[2]) << 8)
                    | static_cast<uint32_t>(payload[3]);
                const uint8_t unit = payload[8];
                if (unit == 1)
                {
                    return static_cast<int>(std::llround(ppmX * 0.0254));
                }
                return -1;
            }
            pos += static_cast<int>(length) + 12;
        }
        return -1;
    }

    /**
     * @brief 统计线条主体（接近白色）的横向跨度（线长）
     *
     * 画布背景为黑、图元描边为白；抗锯齿在两端会留下半强度边缘，
     * 那部分不属于 50mm 的几何长度，用阈值把边缘排除掉。
     */
    int measureDarkWidth(const QImage& image)
    {
        const QImage gray = image.convertToFormat(QImage::Format_Grayscale8);
        int minX = gray.width();
        int maxX = -1;
        for (int y = 0; y < gray.height(); ++y)
        {
            const uint8_t* row = reinterpret_cast<const uint8_t*>(gray.constScanLine(y));
            for (int x = 0; x < gray.width(); ++x)
            {
                if (row[x] > 128)
                {
                    minX = std::min(minX, x);
                    maxX = std::max(maxX, x);
                }
            }
        }
        return (maxX >= minX) ? (maxX - minX + 1) : 0;
    }

    /** @brief 统计图元色（>128）像素的包围盒，空图返回 false */
    bool measureDarkBBox(const QImage& image, int& minX, int& minY, int& maxX, int& maxY)
    {
        const QImage gray = image.convertToFormat(QImage::Format_Grayscale8);
        minX = gray.width();
        minY = gray.height();
        maxX = -1;
        maxY = -1;
        for (int y = 0; y < gray.height(); ++y)
        {
            const uint8_t* row = reinterpret_cast<const uint8_t*>(gray.constScanLine(y));
            for (int x = 0; x < gray.width(); ++x)
            {
                if (row[x] <= 128)
                {
                    continue;
                }
                minX = std::min(minX, x);
                minY = std::min(minY, y);
                maxX = std::max(maxX, x);
                maxY = std::max(maxY, y);
            }
        }
        return maxX >= minX && maxY >= minY;
    }

    /** @brief 统计图元色（>128）像素数量 */
    int countDarkPixels(const QImage& grayImage)
    {
        int count = 0;
        for (int y = 0; y < grayImage.height(); ++y)
        {
            const uint8_t* row = reinterpret_cast<const uint8_t*>(grayImage.constScanLine(y));
            for (int x = 0; x < grayImage.width(); ++x)
            {
                if (row[x] > 128)
                {
                    ++count;
                }
            }
        }
        return count;
    }

    /** @brief 按 QImage 像素格式推断通道数；未知格式返回 -1 */
    int channelCount(const QImage& image)
    {
        switch (image.format())
        {
            case QImage::Format_Grayscale8:
            case QImage::Format_Indexed8:
                return 1;
            case QImage::Format_RGB888:
                return 3;
            case QImage::Format_RGB32:
            case QImage::Format_ARGB32:
            case QImage::Format_ARGB32_Premultiplied:
                return 4;
            default:
                return -1;
        }
    }

    /**
     * @brief 保证进程内已有 Qt 应用实例，供需要重新解码图片的用例使用
     *
     * MainTests 由全局测试环境先建 QApplication，这里直接复用它；
     * 进程内还没有实例时（例如单独只跑本文件这批用例），才懒建一个函数级静态实例。
     * 绝不在用例作用域内构造局部 QCoreApplication：Release 下 Qt 的“唯一实例”
     * 检查是 Q_ASSERT（被编译掉），第二个实例会覆盖 QCoreApplication::self，
     * 用例结束局部对象析构后全局应用状态被清空，后续需要 QWidget 的用例会崩溃。
     */
    void ensureQtApplication()
    {
        if (qApp != nullptr)
        {
            return;
        }

        // 自建实例时 Qt 的插件搜索路径为空，找不到 qjpeg 就无法解码 JPEG；
        // 只在没有外部实例的分支里设置一次，避免覆盖全局环境已经配置好的路径
        static int argc = 1;
        static char appName[] = "RasterExportDpiTests";
        static char* argv[] = { appName, nullptr };
        static QCoreApplication app(argc, argv);
        QCoreApplication::setLibraryPaths({ QCoreApplication::applicationDirPath() });
    }
}  // namespace

TEST(RasterExportDpi, FiftyMillimeterLineIs591PixelsAt300Dpi)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString target = tempDir.filePath(QStringLiteral("line_300dpi.png"));

    Fio::VecSyEntityPtr entities;
    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0.0, 0.0), Ut::Vec2d(50.0, 0.0) });
    entities.push_back(std::move(line));

    ExportContext context;
    context.targetPath = target;
    context.dpi = 300;
    context.scaleFactor = 1.0;

    const ExportResult result = RasterExportHelper::rasterizeExport(context, entities, "PNG");
    ASSERT_TRUE(result.success) << result.message.toStdString();
    ASSERT_TRUE(QFile::exists(target));

    const QImage image(target);
    ASSERT_FALSE(image.isNull());
    const int darkWidth = measureDarkWidth(image);
    const int pngDpi = readPngDpi(target);
    std::printf("[export] darkWidth=%d pngDpi=%d\n", darkWidth, pngDpi);
    // 50mm * 300dpi / 25.4 = 590.55px，四舍五入后 591
    EXPECT_NEAR(darkWidth, 591, 1) << "actual=" << darkWidth;

    EXPECT_NEAR(pngDpi, 300, 1);
}

/**
 * @brief 圆实体导出：像素包围盒直径必须与 DPI 换算一致
 *
 * 直径 20mm、300 DPI：20 * 300 / 25.4 = 236.22 px。
 * 圆按 LineLoop 描边导出，包围盒实测应落在 236 附近（±2px 覆盖抗锯齿与整数取整）。
 */
TEST(RasterExportDpi, CircleBoundingBoxMatchesDpi)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString target = tempDir.filePath(QStringLiteral("circle_300dpi.png"));

    Fio::VecSyEntityPtr entities;
    auto circle = std::make_unique<Eg::SyCircle>();
    circle->basePoint = Ut::Vec2d(10.0, 10.0);
    circle->dRadius = 10.0;
    entities.push_back(std::move(circle));

    ExportContext context;
    context.targetPath = target;
    context.dpi = 300;
    context.scaleFactor = 1.0;

    const ExportResult result = RasterExportHelper::rasterizeExport(context, entities, "PNG");
    ASSERT_TRUE(result.success) << result.message.toStdString();

    const QImage image(target);
    ASSERT_FALSE(image.isNull());
    int minX = 0, minY = 0, maxX = 0, maxY = 0;
    ASSERT_TRUE(measureDarkBBox(image, minX, minY, maxX, maxY)) << "圆未落在画布上";
    const int width = maxX - minX + 1;
    const int height = maxY - minY + 1;
    std::printf("[export-circle] bbox=%dx%d expected=236.22 (20mm@300dpi)\n", width, height);
    EXPECT_NEAR(width, 236, 2) << "actual=" << width;
    EXPECT_NEAR(height, 236, 2) << "actual=" << height;
}

/**
 * @brief 圆弧实体导出：弧段像素覆盖范围必须等于半径对应的像素跨度
 *
 * 半径 10mm、0 → 90 度、300 DPI：半径像素跨度 10 * 300 / 25.4 = 118.11 px。
 * 四分之一圆弧的包围盒宽高都等于半径，且圆心所在的那个角必须落在弧外（背景色）。
 */
TEST(RasterExportDpi, QuarterArcCoversRadiusBoxAndLeavesCenterEmpty)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString target = tempDir.filePath(QStringLiteral("arc_300dpi.png"));

    Fio::VecSyEntityPtr entities;
    auto arc = std::make_unique<Eg::SyArc>();
    arc->basePoint = Ut::Vec2d(10.0, 10.0);
    arc->dRadius = 10.0;
    arc->dStartAngle = 0.0;
    arc->dEndAngle = 1.5707963267948966;
    entities.push_back(std::move(arc));

    ExportContext context;
    context.targetPath = target;
    context.dpi = 300;
    context.scaleFactor = 1.0;

    const ExportResult result = RasterExportHelper::rasterizeExport(context, entities, "PNG");
    ASSERT_TRUE(result.success) << result.message.toStdString();

    const QImage image(target);
    ASSERT_FALSE(image.isNull());
    int minX = 0, minY = 0, maxX = 0, maxY = 0;
    ASSERT_TRUE(measureDarkBBox(image, minX, minY, maxX, maxY)) << "圆弧未落在画布上";
    const int width = maxX - minX + 1;
    const int height = maxY - minY + 1;
    std::printf("[export-arc] bbox=%dx%d expected=118.11 (10mm radius@300dpi)\n", width, height);
    EXPECT_NEAR(width, 118, 2) << "actual=" << width;
    EXPECT_NEAR(height, 118, 2) << "actual=" << height;

    // 弧的两个端点落在包围盒的一对对角上；描边带抗锯齿，端点像素可能被削掉一半，
    // 所以按端点邻域内是否存在图元色判断，而不是只看单个像素。
    const QImage gray = image.convertToFormat(QImage::Format_Grayscale8);
    auto isDark = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= gray.width() || y >= gray.height()) return false;
        return reinterpret_cast<const uint8_t*>(gray.constScanLine(y))[x] > 128;
    };
    auto darkNear = [&](int x, int y, int radius) {
        for (int dy = -radius; dy <= radius; ++dy)
        {
            for (int dx = -radius; dx <= radius; ++dx)
            {
                if (isDark(x + dx, y + dy)) return true;
            }
        }
        return false;
    };
    EXPECT_TRUE(darkNear(minX, minY, 4)) << "起点未落在包围盒左上角";
    EXPECT_TRUE(darkNear(maxX, maxY, 4)) << "终点未落在包围盒右下角";
    // 圆心那个角不应有像素：这段弧不经过圆心一侧
    EXPECT_FALSE(darkNear(minX, maxY, 10)) << "弧外的圆心角落被误填充";
}

/**
 * @brief 闭合四边形导出：FILL_NONE 只描边、FILL_WINDING 填满内部
 *
 * 20mm x 10mm 四边形，300 DPI：外框像素约 236 x 118。
 * 未填充时内部中心为背景（0），填充时中心为图元色（255）。
 */
TEST(RasterExportDpi, ClosedQuadHonoursFillRule)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString outlinePath = tempDir.filePath(QStringLiteral("quad_outline.png"));
    const QString filledPath = tempDir.filePath(QStringLiteral("quad_filled.png"));

    ExportContext context;
    context.dpi = 300;
    context.scaleFactor = 1.0;

    // FILL_NONE：闭合多边形（LineLoop）只描边
    Fio::VecSyEntityPtr outlineEntities;
    auto polygon = std::make_unique<Eg::SyPolygon>();
    polygon->setVertices({ Ut::Vec2d(0.0, 0.0), Ut::Vec2d(20.0, 0.0),
                           Ut::Vec2d(20.0, 10.0), Ut::Vec2d(0.0, 10.0) });
    outlineEntities.push_back(std::move(polygon));
    context.targetPath = outlinePath;
    const ExportResult outlineResult = RasterExportHelper::rasterizeExport(context, outlineEntities, "PNG");
    ASSERT_TRUE(outlineResult.success) << outlineResult.message.toStdString();

    // FILL_WINDING：实心区域（Triangles）按绕数填充
    Fio::VecSyEntityPtr filledEntities;
    auto region = std::make_unique<Eg::SyFillRegion>();
    region->setVertexVector({ Ut::Vec2d(0.0, 0.0), Ut::Vec2d(20.0, 0.0),
                              Ut::Vec2d(20.0, 10.0), Ut::Vec2d(0.0, 10.0) });
    filledEntities.push_back(std::move(region));
    context.targetPath = filledPath;
    const ExportResult filledResult = RasterExportHelper::rasterizeExport(context, filledEntities, "PNG");
    ASSERT_TRUE(filledResult.success) << filledResult.message.toStdString();

    const QImage outlineImage(outlinePath);
    const QImage filledImage(filledPath);
    ASSERT_FALSE(outlineImage.isNull());
    ASSERT_FALSE(filledImage.isNull());

    const QImage outlineGray = outlineImage.convertToFormat(QImage::Format_Grayscale8);
    const QImage filledGray = filledImage.convertToFormat(QImage::Format_Grayscale8);
    ASSERT_EQ(outlineGray.size(), filledGray.size());

    // 世界中心 (10, 5) → 像素 (10*11.811+10, (10-5)*11.811+10) ≈ (128, 69)
    const int centerX = static_cast<int>(std::lround(10.0 * 300.0 / 25.4)) + 10;
    const int centerY = static_cast<int>(std::lround(5.0 * 300.0 / 25.4)) + 10;
    const uint8_t outlineCenter =
        reinterpret_cast<const uint8_t*>(outlineGray.constScanLine(centerY))[centerX];
    const uint8_t filledCenter =
        reinterpret_cast<const uint8_t*>(filledGray.constScanLine(centerY))[centerX];

    const int outlineDark = countDarkPixels(outlineGray);
    const int filledDark = countDarkPixels(filledGray);
    std::printf("[export-quad] center outline=%u filled=%u darkPixels outline=%d filled=%d\n",
                static_cast<unsigned>(outlineCenter), static_cast<unsigned>(filledCenter),
                outlineDark, filledDark);

    // FILL_NONE：内部中心必须仍是背景色
    EXPECT_LT(outlineCenter, 64) << "FILL_NONE 下内部被填充";
    // 20x10mm 的周长约 60mm ≈ 709 像素，描边像素数应远小于面积像素数
    EXPECT_LT(outlineDark, 1500);

    // FILL_WINDING：内部中心必须是图元色
    EXPECT_GT(filledCenter, 128) << "FILL_WINDING 下内部未被填充";
    // 20x10mm 面积 ≈ 236*118 ≈ 27848 像素
    EXPECT_GT(filledDark, 20000);
}

/**
 * @brief 整文件冒烟：PNG 与 JPEG 两条编码路径都可重新读取且尺寸一致
 *
 * 50mm 直线、300 DPI：画布 = ceil(50*300/25.4) + 2*10 = 591 + 20 = 611 像素宽，
 * 高度 = ceil(微小量) + 20 = 1 + 20 = 21 像素。
 */
TEST(RasterExportDpi, PngAndJpegRoundTripSmoke)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    // 重新解码 JPEG 需要 Qt 找得到 qjpeg 插件；复用全局应用实例，
    // 只有进程内还没有实例时才懒建一个静态实例（绝不在用例作用域建局部对象）
    ensureQtApplication();

    const QString pngPath = tempDir.filePath(QStringLiteral("smoke.png"));
    const QString jpegPath = tempDir.filePath(QStringLiteral("smoke.jpg"));

    Fio::VecSyEntityPtr entities;
    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0.0, 0.0), Ut::Vec2d(50.0, 0.0) });
    entities.push_back(std::move(line));

    ExportContext context;
    context.dpi = 300;
    context.scaleFactor = 1.0;

    context.targetPath = pngPath;
    ASSERT_TRUE(RasterExportHelper::rasterizeExport(context, entities, "PNG").success);
    context.targetPath = jpegPath;
    ASSERT_TRUE(RasterExportHelper::rasterizeExport(context, entities, "JPEG").success);

    const QImage pngImage(pngPath);
    const QImage jpegImage(jpegPath);
    ASSERT_FALSE(pngImage.isNull());
    ASSERT_FALSE(jpegImage.isNull());

    const int pngChannels = channelCount(pngImage);
    const int jpegChannels = channelCount(jpegImage);
    std::printf("[export-smoke] png=%dx%d ch=%d depth=%d | jpeg=%dx%d ch=%d depth=%d\n",
                pngImage.width(), pngImage.height(), pngChannels, pngImage.depth(),
                jpegImage.width(), jpegImage.height(), jpegChannels, jpegImage.depth());

    EXPECT_EQ(pngImage.size(), QSize(611, 21));
    EXPECT_EQ(jpegImage.size(), QSize(611, 21));
    EXPECT_EQ(pngChannels, 1) << "PNG 应为单通道灰度";
    EXPECT_EQ(jpegChannels, 1) << "单通道输入编码为灰度 JPEG";
    EXPECT_EQ(pngImage.depth(), 8);
    EXPECT_EQ(jpegImage.depth(), 8);

    // 两条编码路径都能保留同一条 591px 的线段
    const int pngDarkWidth = measureDarkWidth(pngImage);
    const int jpegDarkWidth = measureDarkWidth(jpegImage);
    std::printf("[export-smoke] darkWidth png=%d jpeg=%d expected=591\n", pngDarkWidth, jpegDarkWidth);
    EXPECT_NEAR(pngDarkWidth, 591, 2);
    EXPECT_NEAR(jpegDarkWidth, 591, 2);
}
