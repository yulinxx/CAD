/**
 * @file TransformGizmo3DHitTests.cpp
 * @brief 旋转 gizmo 命中检测回归测试
 *
 * 只盯一条不变量：命中几何必须与渲染几何同源。
 * renderRotate 画的是 drawRing(pivot, kAxes[i], ...) —— 第二个参数是环的【法向】，
 * 所以绕 X 旋转的环躺在 YZ 平面上。hitTestRotate 采样时若用错张成轴，整套命中区
 * 会相对画面错开一位：瞄着环点不中，或者点中了转的是另一根轴。
 * 这个偏差纯几何、无 GL 依赖，可以在这里钉死。
 */

#include <gtest/gtest.h>

#include "UI3D/Render3D/Camera3D.h"
#include "UI3D/Render3D/TransformGizmo3D.h"

#include <cmath>

namespace
{
    constexpr int kViewW = 1200;
    constexpr int kViewH = 800;
    constexpr float kGizmoSize = 100.0f;
    constexpr float kRingRadius = 0.85f * kGizmoSize;  // 与 TransformGizmo3D 的 kRingRadiusRatio 一致

    /// 刻意用非对称视角：(1,1,1) 这类对称方向会让三个环在屏幕上出现巧合重叠，
    /// 测试就不再是在验证映射关系，而是在赌投影结果。
    Camera3D makeCamera()
    {
        Camera3D cam;
        cam.setEyePosition(400.0f, 120.0f, 260.0f);
        cam.setCenter(0.0f, 0.0f, 0.0f);
        cam.setUp(0.0f, 0.0f, 1.0f);
        return cam;
    }

    /// 构造一条从相机眼点穿过指定世界点的射线（该点必然命中，像素距离为 0）
    Eg::Ray3f rayThrough(const Camera3D& cam, const Ut::Vec3f& worldPt)
    {
        const Ut::Vec3f eye(cam.eyeX(), cam.eyeY(), cam.eyeZ());
        return Eg::Ray3f(eye, worldPt - eye);
    }

    /// 绕 axisIdx 旋转的环上取 45° 处的点：该角度只落在自己这一个环上，
    /// 不像 0°/90° 那样正好是两环相交的主轴点
    Ut::Vec3f ringPointAt45(int axisIdx)
    {
        const Ut::Vec3f axes[3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
        const Ut::Vec3f& u = axes[(axisIdx + 1) % 3];
        const Ut::Vec3f& v = axes[(axisIdx + 2) % 3];
        const float c = kRingRadius * std::cos(0.25f * 3.14159265f);
        const float s = kRingRadius * std::sin(0.25f * 3.14159265f);
        return u * c + v * s;
    }

    TransformGizmo3D makeRotateGizmo()
    {
        TransformGizmo3D gizmo;
        gizmo.setMode(Eg::TransformMode::Rotate);
        gizmo.setPivot(Ut::Vec3f(0, 0, 0));
        gizmo.setWorldSize(kGizmoSize);
        return gizmo;
    }
}  // namespace

TEST(TransformGizmo3DHitTest, RotateRingHitReturnsItsOwnAxis)
{
    const Camera3D cam = makeCamera();
    const TransformGizmo3D gizmo = makeRotateGizmo();

    const Eg::TransformAxis expected[3] = { Eg::TransformAxis::X, Eg::TransformAxis::Y, Eg::TransformAxis::Z };
    for (int i = 0; i < 3; ++i)
    {
        const Eg::Ray3f ray = rayThrough(cam, ringPointAt45(i));
        EXPECT_EQ(gizmo.hitTest(ray, cam, kViewW, kViewH), expected[i]) << "ring index " << i;
    }
}

TEST(TransformGizmo3DHitTest, RotateRingPlaneMatchesRenderedNormal)
{
    // 绕 X 的环躺在 YZ 平面上：环上的点 x 分量恒为 0。
    // 反过来，一个 x 分量为 0 的环点若被判成 Y 或 Z，就是采样张成轴用错了。
    const Camera3D cam = makeCamera();
    const TransformGizmo3D gizmo = makeRotateGizmo();

    const Ut::Vec3f pt = ringPointAt45(0);
    ASSERT_FLOAT_EQ(pt[0], 0.0f);
    EXPECT_EQ(gizmo.hitTest(rayThrough(cam, pt), cam, kViewW, kViewH), Eg::TransformAxis::X);
}

TEST(TransformGizmo3DHitTest, RotateMissesWhenFarFromEveryRing)
{
    const Camera3D cam = makeCamera();
    const TransformGizmo3D gizmo = makeRotateGizmo();

    // 远在环外的点：不能因为放宽了像素容差就变成"到处都能抓到环"
    const Eg::Ray3f ray = rayThrough(cam, Ut::Vec3f(0.0f, 6.0f * kGizmoSize, -6.0f * kGizmoSize));
    EXPECT_EQ(gizmo.hitTest(ray, cam, kViewW, kViewH), Eg::TransformAxis::None);
}

TEST(TransformGizmo3DHitTest, RotateCenterBallIsFreeRotateHandle)
{
    // 中心球命中记为 Uniform；旋转模式下视口会把它转译成 TransformAxis::None，
    // 让 RotateTransformer 自己挑最正对相机的轴
    const Camera3D cam = makeCamera();
    const TransformGizmo3D gizmo = makeRotateGizmo();

    const Eg::Ray3f ray = rayThrough(cam, Ut::Vec3f(0.0f, 0.0f, 0.0f));
    EXPECT_EQ(gizmo.hitTest(ray, cam, kViewW, kViewH), Eg::TransformAxis::Uniform);
}

TEST(TransformGizmo3DHitTest, RotateRingWinsOverCenterBall)
{
    // 中心球判定必须排在三环之后：环是主手柄，环上的点不能被中心区抢走
    const Camera3D cam = makeCamera();
    const TransformGizmo3D gizmo = makeRotateGizmo();

    for (int i = 0; i < 3; ++i)
    {
        const Eg::TransformAxis hit = gizmo.hitTest(rayThrough(cam, ringPointAt45(i)), cam, kViewW, kViewH);
        EXPECT_NE(hit, Eg::TransformAxis::Uniform) << "ring index " << i;
    }
}

