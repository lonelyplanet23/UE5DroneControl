#pragma once

#include <cstdint>

/// NED ↔ UE 坐标转换工具
///
/// NED:   X=North(北), Y=East(东), Z=Down(下)    单位: 米
/// UE5:   X=Forward(前), Y=Right(右), Z=Up(上)    单位: 厘米
///
/// 转换公式:
///   UE_X = NED_N × 100        北 → 前
///   UE_Y = NED_E × 100        东 → 右
///   UE_Z = -NED_D × 100       下 → 上（取反）
class CoordinateConverter {
public:
    /// NED 米 → UE 偏移厘米（用于遥测：NED → UE 推送）
    static void NedToUeOffset(double ned_n, double ned_e, double ned_d,
                              double& out_x, double& out_y, double& out_z);

    /// UE 偏移厘米 → NED 米（用于控制：UE move → NED 指令）
    static void UeOffsetToNed(double ue_x, double ue_y, double ue_z,
                              double& out_n, double& out_e, double& out_d);

    static constexpr double NED_TO_UE_SCALE = 100.0;
    static constexpr double UE_TO_NED_SCALE = 0.01;
};
