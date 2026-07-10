#pragma once

#include <cstdint>
#include <atomic>
#include <chrono>
#include <functional>
#include <string>

enum class DroneConnectionState : uint8_t {
    Offline,
    Connecting,
    Online,
    Lost
};

enum class StateEvent : uint8_t {
    PowerOn,
    LostConnection,
    Reconnect
};

/// 后端内部控制指令。
///
/// UDP 线上格式由 UdpSender 序列化为 JSON；这里不再是线上的二进制结构体。
/// x/y/z 始终是以无人机本次上电位置为原点的 NED 米坐标。
struct DroneControlPacket {
    uint64_t sequence = 0;     // 单调递增逻辑指令序号，用于 Jetson 拒绝旧指令
    double   timestamp = 0.0;
    float    x = 0.0f;
    float    y = 0.0f;
    float    z = 0.0f;
    uint32_t mode = 0;         // 0=hold/heartbeat, 1=move
    int      slot = 0;
    uint32_t repeat_index = 0; // 本逻辑指令的第几次发送（从 1 开始）
    uint32_t repeat_total = 0; // move 为有限重发次数；hold=0 表示持续发送
};

inline uint64_t NextControlSequence()
{
    static std::atomic<uint64_t> sequence{
        static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count())
    };
    return sequence.fetch_add(1, std::memory_order_relaxed);
}

struct TelemetryData {
    uint64_t timestamp = 0;
    double position_ned[3] = {0.0, 0.0, 0.0};
    double quaternion[4] = {1.0, 0.0, 0.0, 0.0};
    double velocity[3] = {0.0, 0.0, 0.0};
    double angular_velocity[3] = {0.0, 0.0, 0.0};

    int battery = -1;

    double gps_lat = 0.0;
    double gps_lon = 0.0;
    double gps_alt = 0.0;
    bool gps_fix = false;

    double local_position[3] = {0.0, 0.0, 0.0};

    uint8_t arming_state = 0; // 1=DISARMED, 2=ARMED
    uint8_t nav_state = 0;    // 14=OFFBOARD

    // Jetson 在达到 UDP 指令确认阈值并应用 setpoint 后随遥测返回。
    std::string control_ack_session_id;
    std::string control_ack_command_id;
    uint64_t control_ack_sequence = 0;
    std::string control_ack_mode;
    uint32_t control_ack_confirmed_packets = 0;

    bool IsArmed() const { return arming_state == 2; }
    bool IsOffboard() const { return nav_state == 14; }
};

struct GpsAnchor {
    int drone_id = 0;
    double latitude = 0.0;
    double longitude = 0.0;
    double altitude = 0.0;
    bool valid = false;
};

struct PortMapping {
    int slot = 0;
    int recv_port = 0;
    int send_port = 0;
    std::string ros_topic_prefix;
};

struct DroneStatus {
    std::string id;
    std::string name;
    int slot = 0;
    std::string connection_state;
    int battery = -1;
    double pos_x = 0.0;
    double pos_y = 0.0;
    double pos_z = 0.0;
    double yaw = 0.0;
    double speed = 0.0;
    GpsAnchor anchor;
    std::string control_ack_command_id;
    uint64_t control_ack_sequence = 0;
};

struct AssemblyProgress {
    std::string array_id;
    int ready_count = 0;
    int total_count = 0;
};

/// Backend-authoritative high-level task state pushed to UE over WebSocket.
struct DroneTaskState {
    int drone_id = 0;
    std::string array_id;
    std::string mode = "move";
    std::string state = "standby";
    int current_wp = 0;
    int waypoint_count = 0;
    std::string detail;
    double updated_at = 0.0;
};

using TelemetryCallback = std::function<void(int drone_id, const TelemetryData&)>;
using StateChangeCallback = std::function<void(int drone_id, StateEvent event, const GpsAnchor&)>;
using AlertCallback = std::function<void(int drone_id, const std::string& alert_type, int value)>;
using AssemblyCallback = std::function<void(const AssemblyProgress&)>;
using TaskStateCallback = std::function<void(const DroneTaskState&)>;
