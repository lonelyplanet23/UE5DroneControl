#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <functional>

// ============================================================
// 连接状态
// ============================================================
enum class DroneConnectionState : uint8_t {
    Offline,      // 初始/离线
    Connecting,   // 收到首包但未稳定
    Online,       // 正常工作
    Lost          // 超时失联
};

// ============================================================
// 状态变更事件（供 WebSocket 推送）
// ============================================================
enum class StateEvent : uint8_t {
    PowerOn,          // Offline→Online（首包），触发锚点记录
    LostConnection,   // Online→Lost（10s 超时）
    Reconnect         // Lost→Online（重连），触发锚点更新
};

// ============================================================
// 24 字节 UDP 控制包（后端 → 无人机）
// 小端序，#pragma pack(1) 对齐
// ============================================================
#pragma pack(push, 1)
struct DroneControlPacket {
    double   timestamp;  // 8B, Unix 时间戳（秒）
    float    x;          // 4B, NED North（米）
    float    y;          // 4B, NED East（米）
    float    z;          // 4B, NED Down（米）
    uint32_t mode;       // 4B, 0=悬停/心跳, 1=移动
    // 总计 24 字节
};
#pragma pack(pop)

// ============================================================
// 遥测数据（Jetson 桥接 → 后端，YAML 解析后）
// ============================================================
struct TelemetryData {
    uint64_t  timestamp;            // PX4 系统时间（微秒）
    double    position_ned[3];      // [N, E, D] 米 — vehicle_odometry
    double    quaternion[4];        // [w, x, y, z] — vehicle_odometry
    double    velocity[3];          // [vN, vE, vD] m/s — vehicle_odometry
    double    angular_velocity[3];  // [roll, pitch, yaw] rad/s — vehicle_odometry

    int       battery;              // %, -1=不可用 — battery_status

    double    gps_lat;              // 度 — vehicle_global_position
    double    gps_lon;              // 度
    double    gps_alt;              // 米（椭球高）
    bool      gps_fix;              // GPS 是否有效

    double    local_position[3];    // [x, y, z] 米 — vehicle_local_position

    uint8_t   arming_state;         // 1=DISARMED, 2=ARMED — vehicle_status_v1
    uint8_t   nav_state;            // 14=OFFBOARD — vehicle_status_v1

    // 辅助判断
    bool IsArmed()    const { return arming_state == 2; }
    bool IsOffboard() const { return nav_state == 14; }
};

// ============================================================
// GPS 锚点
// ============================================================
struct GpsAnchor {
    int     drone_id = 0;
    double  latitude = 0.0;
    double  longitude = 0.0;
    double  altitude = 0.0;
    bool    valid = false;
};

// ============================================================
// 端口映射
// ============================================================
struct PortMapping {
    int     slot = 0;              // 编号 1~6
    int     recv_port = 0;         // 遥测接收端口（后端监听）
    int     send_port = 0;         // 控制发送端口（后端→Jetson）
    std::string ros_topic_prefix;  // e.g. "/px4_1"
};

// ============================================================
// 无人机状态快照（供 HTTP GET /api/drones 返回）
// ============================================================
struct DroneStatus {
    std::string id;                 // d1, d2, ...
    std::string name;
    int         slot = 0;
    std::string connection_state;   // "offline" / "connecting" / "online" / "lost"
    int         battery = -1;
    double      pos_x = 0.0;        // UE 偏移坐标 X（厘米）
    double      pos_y = 0.0;        // UE 偏移坐标 Y（厘米）
    double      pos_z = 0.0;        // UE 偏移坐标 Z（厘米）
    double      yaw = 0.0;          // 度
    double      speed = 0.0;        // m/s
    GpsAnchor   anchor;
};

// ============================================================
// 集结进度
// ============================================================
struct AssemblyProgress {
    std::string array_id;
    int         ready_count = 0;
    int         total_count = 0;
};

// ============================================================
// 回调类型（供 WebSocket Server 注册）
// ============================================================
using TelemetryCallback    = std::function<void(int drone_id, const TelemetryData&)>;
using StateChangeCallback  = std::function<void(int drone_id, StateEvent event, const GpsAnchor&)>;
using AlertCallback        = std::function<void(int drone_id, const std::string& alert_type, int value)>;
using AssemblyCallback     = std::function<void(const AssemblyProgress&)>;
