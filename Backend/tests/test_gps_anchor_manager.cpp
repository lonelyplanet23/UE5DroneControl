#include <gtest/gtest.h>
#include "conversion/gps_anchor_manager.h"

TEST(GpsAnchorManagerTest, SetAndGetAnchor)
{
    GpsAnchorManager mgr;

    // 初始无锚点
    EXPECT_FALSE(mgr.HasAnchor(1));

    // 设置锚点
    bool is_new = mgr.SetAnchor(1, 39.9042, 116.4074, 50.0);
    EXPECT_TRUE(is_new);  // 首次设置 → 新锚点

    EXPECT_TRUE(mgr.HasAnchor(1));
    EXPECT_EQ(mgr.Count(), 1);

    auto anchor = mgr.GetAnchor(1);
    EXPECT_DOUBLE_EQ(anchor.latitude, 39.9042);
    EXPECT_DOUBLE_EQ(anchor.longitude, 116.4074);
    EXPECT_DOUBLE_EQ(anchor.altitude, 50.0);
    EXPECT_TRUE(anchor.valid);
}

TEST(GpsAnchorManagerTest, UpdateAnchor)
{
    GpsAnchorManager mgr;

    mgr.SetAnchor(1, 39.9042, 116.4074, 50.0);
    // 再次设置 → 更新
    bool is_new = mgr.SetAnchor(1, 39.9043, 116.4075, 50.5);
    EXPECT_FALSE(is_new);  // 更新 → 不是新的

    auto anchor = mgr.GetAnchor(1);
    EXPECT_DOUBLE_EQ(anchor.latitude, 39.9043);
    EXPECT_DOUBLE_EQ(anchor.longitude, 116.4075);
}

TEST(GpsAnchorManagerTest, ClearAnchor)
{
    GpsAnchorManager mgr;
    mgr.SetAnchor(1, 39.9042, 116.4074, 50.0);
    EXPECT_EQ(mgr.Count(), 1);

    mgr.ClearAnchor(1);
    EXPECT_FALSE(mgr.HasAnchor(1));
    EXPECT_EQ(mgr.Count(), 0);
}

TEST(GpsAnchorManagerTest, MultipleDrones)
{
    GpsAnchorManager mgr;
    mgr.SetAnchor(1, 39.9, 116.4, 50.0);
    mgr.SetAnchor(2, 40.0, 116.5, 60.0);

    EXPECT_EQ(mgr.Count(), 2);

    auto a1 = mgr.GetAnchor(1);
    auto a2 = mgr.GetAnchor(2);
    EXPECT_DOUBLE_EQ(a1.latitude, 39.9);
    EXPECT_DOUBLE_EQ(a2.latitude, 40.0);
}

TEST(GpsAnchorManagerTest, GetNonExistent)
{
    GpsAnchorManager mgr;
    auto anchor = mgr.GetAnchor(999);
    EXPECT_FALSE(anchor.valid);
    EXPECT_EQ(anchor.drone_id, 0);
}
