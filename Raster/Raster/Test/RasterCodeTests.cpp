/**
 * @file RasterCodeTests.cpp
 * @brief 码识别测试：二维码检测、解码内容校验、参数校验。
 *
 * 用 OpenCV 的 QRCodeEncoder 在测试里现场生成二维码（不依赖任何素材文件），
 * 再走一遍本模块的识别接口。
 */

#include "RasterTestSupport.h"

#include <gtest/gtest.h>

#include <opencv2/imgproc.hpp>
#include <opencv2/objdetect.hpp>

using namespace raster_test;

namespace
{
    /** @brief 每个模块放大到多少像素。 */
    constexpr double kModuleScale = 4.0;

    /** @brief 生成一张带二维码的图像（黑色码 + 白色留白边框）。 */
    ImageGuard makeQrImage(const std::string& content, int quietZone = 4)
    {
        cv::QRCodeEncoder::Params params;
        params.version = 0;  // 0 = 按内容长度自动选版本
        params.correction_level = cv::QRCodeEncoder::CORRECT_LEVEL_L;
        params.mode = cv::QRCodeEncoder::MODE_BYTE;
        cv::Ptr<cv::QRCodeEncoder> encoder = cv::QRCodeEncoder::create(params);

        cv::Mat qr;
        encoder->encode(content, qr);
        // 编码器输出的是 0/1，缩放到 0/255（已经是 0/255 时会被饱和截断，结果一致）。
        cv::Mat normalized;
        qr.convertTo(normalized, CV_8U, 255.0);

        // 必须放大：1 像素/模块的二维码 OpenCV 能"检测到"但**解不出来**
        // （定位图案与数据模块都退化到 1 像素）。实测 2 倍即可正常解码。
        // 激光打标的码在实物上是 mm 级的，拍照后自然是多像素/模块。
        cv::Mat scaled;
        cv::resize(normalized, scaled, cv::Size(), kModuleScale, kModuleScale, cv::INTER_NEAREST);

        const int margin = quietZone;
        cv::Mat canvas(scaled.rows + margin * 2, scaled.cols + margin * 2, CV_8UC1,
                       cv::Scalar::all(255));
        scaled.copyTo(canvas(cv::Rect(margin, margin, scaled.cols, scaled.rows)));

        RasterImage handle = nullptr;
        expectOk(Raster_CreateImage(canvas.cols, canvas.rows, RASTER_FORMAT_GRAY8, &handle),
                  "Raster_CreateImage");
        ImageGuard guard(handle);

        RasterImageView view{};
        view.structSize = sizeof(RasterImageView);
        expectOk(Raster_GetImageView(handle, &view), "Raster_GetImageView");
        auto* pixels = static_cast<uint8_t*>(const_cast<void*>(view.data));
        std::memcpy(pixels, canvas.data, static_cast<size_t>(canvas.total()));
        return guard;
    }

    TEST(RasterCode, DetectsQrCodeAndDecodesContent)
    {
        auto image = makeQrImage("SN-2026-0001");
        RasterCodeSet set = nullptr;
        ASSERT_EQ(Raster_DetectCodes(image.get(), &set), RASTER_OK);
        CodeSetGuard guard(set);

        int32_t count = 0;
        ASSERT_EQ(Raster_CodeSetCount(set, &count), RASTER_OK);
        ASSERT_GE(count, 1);

        RasterCode code{};
        code.structSize = sizeof(RasterCode);
        ASSERT_EQ(Raster_CodeSetGet(set, 0, &code), RASTER_OK);
        EXPECT_EQ(code.kind, static_cast<int32_t>(RASTER_CODE_QR));
        ASSERT_NE(code.text, nullptr);
        EXPECT_STREQ(code.text, "SN-2026-0001");
        EXPECT_GT(code.width, 0);
        EXPECT_GT(code.height, 0);
    }

    TEST(RasterCode, VerifyCodeAcceptsMatchingContent)
    {
        auto image = makeQrImage("PART-42");
        int32_t matched = 0;
        ASSERT_EQ(Raster_VerifyCode(image.get(), "PART-42", &matched), RASTER_OK);
        EXPECT_EQ(matched, 1);
    }

    TEST(RasterCode, VerifyCodeRejectsMismatchedContent)
    {
        auto image = makeQrImage("PART-42");
        int32_t matched = 1;
        ASSERT_EQ(Raster_VerifyCode(image.get(), "PART-43", &matched), RASTER_OK);
        EXPECT_EQ(matched, 0);
    }

    TEST(RasterCode, VerifyCodeReportsMissingCode)
    {
        // 全白图里没有码。
        auto blank = makeCheckerboard(64, 64, 64);
        int32_t matched = 1;
        EXPECT_EQ(Raster_VerifyCode(blank.get(), "PART-42", &matched), RASTER_E_NOT_FOUND);
        EXPECT_EQ(matched, 0);
    }

    TEST(RasterCode, VerifyCodeRejectsEmptyExpectation)
    {
        auto image = makeQrImage("PART-42");
        int32_t matched = 0;
        EXPECT_EQ(Raster_VerifyCode(image.get(), "", &matched), RASTER_E_INVALID_ARG);
        EXPECT_EQ(Raster_VerifyCode(image.get(), nullptr, &matched), RASTER_E_INVALID_ARG);
    }

    TEST(RasterCode, CodeSetGetWithOutOfRangeIndexFails)
    {
        auto image = makeQrImage("X");
        RasterCodeSet set = nullptr;
        ASSERT_EQ(Raster_DetectCodes(image.get(), &set), RASTER_OK);
        CodeSetGuard guard(set);

        RasterCode code{};
        code.structSize = sizeof(RasterCode);
        EXPECT_EQ(Raster_CodeSetGet(set, 999, &code), RASTER_E_NOT_FOUND);
    }

    TEST(RasterCode, DestroyingNullHandleIsSafe)
    {
        Raster_DestroyCodeSet(nullptr);
    }
}  // namespace
