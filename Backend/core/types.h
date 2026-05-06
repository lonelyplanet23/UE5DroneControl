#pragma once

#include <cstdint>
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

#pragma pack(push, 1)
struct DroneControlPacket {
    double   timestamp = 0.0;
    float    x = 0.0f;
    float    y = 0.0f;
    float    z = 0.0f;
    uint32_t mode = 0;       // 0=hover/heartbeat, 1=move
};
#pragma pack(pop)

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
};

struct AssemblyProgress {
    std::string array_id;
    int ready_count = 0;
    int total_count = 0;
};

using TelemetryCallback = std::function<void(int drone_id, const TelemetryData&)>;
using StateChangeCallback = std::function<void(int drone_id, StateEvent event, const GpsAnchor&)>;
using AlertCallback = std::function<void(int drone_id, const std::string& alert_type, int value)>;
using AssemblyCallback = std::function<void(const AssemblyProgress&)>;
