#pragma once

#include <chrono>
#include <cmath>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include <boost/json.hpp>

constexpr double kPi = 3.14159265358979323846;

enum class DroneStatus {
    offline,
    connecting,
    online,
    lost,
};

inline std::string to_string(DroneStatus status) {
    switch (status) {
        case DroneStatus::offline:
            return "offline";
        case DroneStatus::connecting:
            return "connecting";
        case DroneStatus::online:
            return "online";
        case DroneStatus::lost:
            return "lost";
    }
    return "offline";
}

enum class ArrayStatus {
    assembling,
    executing,
    done,
    stopped,
    timeout,
};

inline std::string to_lower_string(ArrayStatus status) {
    switch (status) {
        case ArrayStatus::assembling:
            return "assembling";
        case ArrayStatus::executing:
            return "executing";
        case ArrayStatus::done:
            return "done";
        case ArrayStatus::stopped:
            return "stopped";
        case ArrayStatus::timeout:
            return "timeout";
    }
    return "assembling";
}

inline std::string to_upper_string(ArrayStatus status) {
    switch (status) {
        case ArrayStatus::assembling:
            return "ASSEMBLING";
        case ArrayStatus::executing:
            return "EXECUTING";
        case ArrayStatus::done:
            return "DONE";
        case ArrayStatus::stopped:
            return "STOPPED";
        case ArrayStatus::timeout:
            return "TIMEOUT";
    }
    return "ASSEMBLING";
}

struct GeoAnchor {
    bool available = false;
    double gps_lat = 0.0;
    double gps_lon = 0.0;
    double gps_alt = 0.0;

    boost::json::object to_json(const std::string& drone_id) const {
        return {
            {"drone_id", drone_id},
            {"gps_lat", gps_lat},
            {"gps_lon", gps_lon},
            {"gps_alt", gps_alt},
        };
    }
};

struct NedPoint {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct UePoint {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct QuaternionWxyz {
    double w = 1.0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct TelemetryFrame {
    std::uint64_t timestamp_us = 0;
    NedPoint position;
    QuaternionWxyz q;
    NedPoint velocity;
    NedPoint angular_velocity;
    int battery = -1;
    std::optional<GeoAnchor> anchor;
};

struct CommandTarget {
    NedPoint ned;
    std::uint32_t mode = 1;
    std::string source = "manual";
    int path_id = 0;
    double segment_speed = 0.0;
    double wait_time = 0.0;

    boost::json::object to_json() const {
        return {
            {"x", ned.x},
            {"y", ned.y},
            {"z", ned.z},
            {"mode", static_cast<std::int64_t>(mode)},
            {"source", source},
            {"path_id", path_id},
            {"segment_speed", segment_speed},
            {"wait_time", wait_time},
        };
    }
};

struct ArrayPathState {
    int path_id = 0;
    std::string drone_id;
    std::string mode = "recon";
    bool closed_loop = false;
    std::vector<CommandTarget> plan;
    std::size_t current_index = 0;
    bool ready = false;
    bool completed = false;
    bool target_override_active = false;
    CommandTarget target_override;

    boost::json::object to_json() const {
        boost::json::array plan_json;
        for (const auto& item : plan) {
            plan_json.push_back(item.to_json());
        }

        boost::json::object object{
            {"path_id", path_id},
            {"drone_id", drone_id},
            {"mode", mode},
            {"closed_loop", closed_loop},
            {"current_index", static_cast<std::int64_t>(current_index)},
            {"ready", ready},
            {"completed", completed},
            {"target_override_active", target_override_active},
            {"plan", plan_json},
        };

        if (target_override_active) {
            object["target_override"] = target_override.to_json();
        }

        return object;
    }
};

struct ArrayTask {
    std::string array_id;
    std::string mode = "recon";
    ArrayStatus status = ArrayStatus::assembling;
    std::size_t ready_count = 0;
    std::size_t total_count = 0;
    bool timeout_emitted = false;
    std::chrono::steady_clock::time_point created_at = std::chrono::steady_clock::now();
    std::vector<ArrayPathState> paths;

    boost::json::object to_json() const {
        boost::json::array path_json;
        for (const auto& path : paths) {
            path_json.push_back(path.to_json());
        }

        return {
            {"array_id", array_id},
            {"mode", mode},
            {"status", to_upper_string(status)},
            {"ready_count", static_cast<std::int64_t>(ready_count)},
            {"total_count", static_cast<std::int64_t>(total_count)},
            {"paths", path_json},
        };
    }
};

struct DroneRecord {
    std::string id;
    std::string name;
    std::string model;
    int slot = 0;
    std::string ip;
    int port = 0;
    std::string video_url;
    DroneStatus status = DroneStatus::offline;
    int battery = -1;
    GeoAnchor anchor;

    boost::json::object to_public_json() const {
        return {
            {"id", id},
            {"name", name},
            {"model", model},
            {"slot", slot},
            {"ip", ip},
            {"port", port},
            {"video_url", video_url},
            {"status", to_string(status)},
            {"battery", battery},
        };
    }

    boost::json::object to_storage_json() const {
        return {
            {"id", id},
            {"name", name},
            {"model", model},
            {"slot", slot},
            {"ip", ip},
            {"port", port},
            {"video_url", video_url},
        };
    }
};

struct DroneRuntime {
    DroneRecord record;
    std::deque<CommandTarget> queue;
    std::optional<TelemetryFrame> last_telemetry;
    std::optional<std::chrono::steady_clock::time_point> last_telemetry_at;
    std::optional<std::chrono::steady_clock::time_point> first_heartbeat_at;
    std::optional<std::chrono::steady_clock::time_point> last_heartbeat_at;
    bool paused = false;
    bool low_battery_alerted = false;
    bool lost_alerted = false;
    std::uint64_t heartbeat_count = 0;
    std::string active_array_id;
};

inline UePoint ned_to_ue(const NedPoint& ned) {
    return {ned.x * 100.0, ned.y * 100.0, ned.z * -100.0};
}

inline NedPoint ue_to_ned(double x_cm, double y_cm, double z_cm) {
    return {x_cm * 0.01, y_cm * 0.01, z_cm * -0.01};
}

inline double distance_m(const NedPoint& a, const NedPoint& b) {
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    const double dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

inline double speed_mps(const NedPoint& velocity) {
    return std::sqrt(
        velocity.x * velocity.x +
        velocity.y * velocity.y +
        velocity.z * velocity.z
    );
}

inline double rad_to_deg(double radians) {
    return radians * 180.0 / kPi;
}

inline std::tuple<double, double, double> quaternion_to_ue_euler_deg(const QuaternionWxyz& q) {
    const double w = q.w;
    const double x = q.x;
    const double y = q.y;
    const double z = -q.z;

    const double sinr_cosp = 2.0 * (w * x + y * z);
    const double cosr_cosp = 1.0 - 2.0 * (x * x + y * y);
    const double roll = std::atan2(sinr_cosp, cosr_cosp);

    const double sinp = 2.0 * (w * y - z * x);
    const double pitch = std::abs(sinp) >= 1.0
        ? std::copysign(kPi / 2.0, sinp)
        : std::asin(sinp);

    const double siny_cosp = 2.0 * (w * z + x * y);
    const double cosy_cosp = 1.0 - 2.0 * (y * y + z * z);
    const double yaw = std::atan2(siny_cosp, cosy_cosp);

    return {
        rad_to_deg(yaw),
        rad_to_deg(pitch),
        rad_to_deg(roll),
    };
}
