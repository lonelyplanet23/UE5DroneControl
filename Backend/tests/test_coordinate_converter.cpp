#include <gtest/gtest.h>
#include "conversion/coordinate_converter.h"

// NED → UE 偏移: N=North, E=East, D=Down → X=Forward, Y=Right, Z=Up
// 米 → 厘米 ×100, Z 取反
TEST(CoordinateConverterTest, NedToUeOffset)
{
    double x, y, z;

    // NED (1, 2, -3) → NED 中 Down=-3 表示"上"
    // UE: X=1*100=100, Y=2*100=200, Z=-(-3)*100=300
    CoordinateConverter::NedToUeOffset(1.0, 2.0, -3.0, x, y, z);
    EXPECT_DOUBLE_EQ(x, 100.0);
    EXPECT_DOUBLE_EQ(y, 200.0);
    EXPECT_DOUBLE_EQ(z, 300.0);

    // NED (0, 0, 5) → 向下 5 米 → UE Z = -5*100 = -500
    CoordinateConverter::NedToUeOffset(0.0, 0.0, 5.0, x, y, z);
    EXPECT_DOUBLE_EQ(x, 0.0);
    EXPECT_DOUBLE_EQ(y, 0.0);
    EXPECT_DOUBLE_EQ(z, -500.0);
}

// UE 偏移 → NED: X=Forward, Y=Right, Z=Up → N=North, E=East, D=Down
// 厘米 → 米 ×0.01, Z 取反
TEST(CoordinateConverterTest, UeOffsetToNed)
{
    double n, e, d;

    // UE (100, 200, 300) → NED: N=100*0.01=1, E=200*0.01=2, D=-300*0.01=-3
    CoordinateConverter::UeOffsetToNed(100.0, 200.0, 300.0, n, e, d);
    EXPECT_DOUBLE_EQ(n, 1.0);
    EXPECT_DOUBLE_EQ(e, 2.0);
    EXPECT_DOUBLE_EQ(d, -3.0);

    // UE (0, 0, -500) → NED: N=0, E=0, D=-(-500)*0.01=5
    CoordinateConverter::UeOffsetToNed(0.0, 0.0, -500.0, n, e, d);
    EXPECT_DOUBLE_EQ(n, 0.0);
    EXPECT_DOUBLE_EQ(e, 0.0);
    EXPECT_DOUBLE_EQ(d, 5.0);
}

// 往返验证: UE → NED → UE 应回到原点
TEST(CoordinateConverterTest, RoundTrip)
{
    double ue_x = 1234.0, ue_y = 5678.0, ue_z = 901.0;

    double n, e, d;
    CoordinateConverter::UeOffsetToNed(ue_x, ue_y, ue_z, n, e, d);

    double rx, ry, rz;
    CoordinateConverter::NedToUeOffset(n, e, d, rx, ry, rz);

    EXPECT_NEAR(rx, ue_x, 1e-9);
    EXPECT_NEAR(ry, ue_y, 1e-9);
    EXPECT_NEAR(rz, ue_z, 1e-9);
}
