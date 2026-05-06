#include "quaternion_utils.h"
#include <cmath>

namespace {
constexpr double kPi = 3.14159265358979323846;
}

void QuaternionUtils::QuatToEuler(double qw, double qx, double qy, double qz,
                                   double& roll, double& pitch, double& yaw)
{
    // 步骤1: Z 分量取反（NED Down → UE5 Up）
    double ue_qx = qx;
    double ue_qy = qy;
    double ue_qz = -qz;
    double ue_qw = qw;

    // 步骤2: 四元数 → 欧拉角（标准公式）
    double sin_r_cos_p = 2.0 * (ue_qw * ue_qx + ue_qy * ue_qz);
    double cos_r_cos_p = 1.0 - 2.0 * (ue_qx * ue_qx + ue_qy * ue_qy);
    roll = std::atan2(sin_r_cos_p, cos_r_cos_p);

    double sin_p = 2.0 * (ue_qw * ue_qy - ue_qz * ue_qx);
    if (std::abs(sin_p) >= 1.0)
        pitch = std::copysign(kPi / 2.0, sin_p);
    else
        pitch = std::asin(sin_p);

    double sin_y_cos_p = 2.0 * (ue_qw * ue_qz + ue_qx * ue_qy);
    double cos_y_cos_p = 1.0 - 2.0 * (ue_qy * ue_qy + ue_qz * ue_qz);
    yaw = std::atan2(sin_y_cos_p, cos_y_cos_p);

    // 步骤3: Yaw 取反（右手系 → 左手系）
    // The qz flip above already converts positive NED yaw to negative UE yaw.

    // 弧度 → 度
    roll  *= (180.0 / kPi);
    pitch *= (180.0 / kPi);
    yaw   *= (180.0 / kPi);
}

double QuaternionUtils::GetUeYaw(double qw, double qx, double qy, double qz)
{
    double roll, pitch, yaw;
    QuatToEuler(qw, qx, qy, qz, roll, pitch, yaw);
    return yaw;
}
