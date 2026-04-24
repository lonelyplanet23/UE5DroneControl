#include "coordinate_converter.h"

void CoordinateConverter::NedToUeOffset(double ned_n, double ned_e, double ned_d,
                                         double& out_x, double& out_y, double& out_z)
{
    out_x = ned_n * NED_TO_UE_SCALE;   // 北 → 前，米→厘米
    out_y = ned_e * NED_TO_UE_SCALE;   // 东 → 右，米→厘米
    out_z = -ned_d * NED_TO_UE_SCALE;  // 下 → 上（取反），米→厘米
}

void CoordinateConverter::UeOffsetToNed(double ue_x, double ue_y, double ue_z,
                                          double& out_n, double& out_e, double& out_d)
{
    out_n = ue_x * UE_TO_NED_SCALE;    // 前 → 北，厘米→米
    out_e = ue_y * UE_TO_NED_SCALE;    // 右 → 东，厘米→米
    out_d = -ue_z * UE_TO_NED_SCALE;   // 上 → 下（取反），厘米→米
}
