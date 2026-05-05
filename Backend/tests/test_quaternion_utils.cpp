#include <gtest/gtest.h>
#include "conversion/quaternion_utils.h"
#include <cmath>

namespace {
constexpr double kPi = 3.14159265358979323846;
}

// 速度计算: sqrt(vN² + vE² + vD²)
TEST(QuaternionUtilsTest, SpeedFromVelocity)
{
    double speed = QuaternionUtils::SpeedFromVelocity(3.0, 4.0, 0.0);
    EXPECT_NEAR(speed, 5.0, 1e-9);  // 3-4-5 三角形

    speed = QuaternionUtils::SpeedFromVelocity(1.0, 1.0, 1.0);
    EXPECT_NEAR(speed, std::sqrt(3.0), 1e-9);
}

// 单位四元数: (1,0,0,0) → 所有角度为 0
TEST(QuaternionUtilsTest, IdentityQuaternion)
{
    double roll, pitch, yaw;
    QuaternionUtils::QuatToEuler(1.0, 0.0, 0.0, 0.0, roll, pitch, yaw);
    EXPECT_NEAR(roll, 0.0, 1e-6);
    EXPECT_NEAR(pitch, 0.0, 1e-6);
    EXPECT_NEAR(yaw, 0.0, 1e-6);
}

// Yaw 取反测试: 旋转 Z 轴 30 度
// NED Yaw = 30°, UE Yaw = -30°
TEST(QuaternionUtilsTest, YawFlip)
{
    // 绕 Z 轴旋转 30°: q = cos(15°) + sin(15°)*k
    // 四元数: [cos(15°), 0, 0, sin(15°)]
    double deg15 = 15.0 * kPi / 180.0;
    double qw = std::cos(deg15);
    double qz = std::sin(deg15);

    double roll, pitch, yaw;
    QuaternionUtils::QuatToEuler(qw, 0.0, 0.0, qz, roll, pitch, yaw);

    // NED Yaw = 30°, UE Yaw 应为 -30°
    EXPECT_NEAR(yaw, -30.0, 1e-3);
    EXPECT_NEAR(roll, 0.0, 1e-3);
    EXPECT_NEAR(pitch, 0.0, 1e-3);
}

TEST(QuaternionUtilsTest, GetUeYaw)
{
    double deg15 = 15.0 * kPi / 180.0;
    double yaw = QuaternionUtils::GetUeYaw(std::cos(deg15), 0.0, 0.0, std::sin(deg15));
    EXPECT_NEAR(yaw, -30.0, 1e-3);
}
