#pragma once

#include <cmath>

/// 四元数工具：NED/FRD → UE5 左手系转换
class QuaternionUtils {
public:
    /// 计算标量速度 (m/s)
    /// @param vN  NED 北向速度 (m/s)
    /// @param vE  NED 东向速度 (m/s)
    /// @param vD  NED 地向速度 (m/s)
    static double SpeedFromVelocity(double vN, double vE, double vD)
    {
        return std::sqrt(vN * vN + vE * vE + vD * vD);
    }

    /// NED 四元数 → UE5 欧拉角
    ///
    /// 输入: 四元数 [w, x, y, z]（来自 PX4 / YAML 顺序）
    /// 转换步骤:
    ///   1. 重排为 FQuat(x, y, -z, w) —— Z 分量取反适配左手系
    ///   2. UE Yaw = -NED Yaw —— Yaw 取反
    ///
    /// @param qw, qx, qy, qz  输入四元数（YAML 顺序: w, x, y, z）
    /// @param[out] roll        横滚角（度）
    /// @param[out] pitch       俯仰角（度）
    /// @param[out] yaw         偏航角（度，已翻转）
    static void QuatToEuler(double qw, double qx, double qy, double qz,
                            double& roll, double& pitch, double& yaw);

    /// 简化为只获取 UE Yaw（最常用）
    static double GetUeYaw(double qw, double qx, double qy, double qz);
};
