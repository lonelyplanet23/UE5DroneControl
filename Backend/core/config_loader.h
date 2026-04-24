#pragma once

#include "core/types.h"
#include <string>
#include <vector>
#include <unordered_map>

/// 后端配置结构体
struct AppConfig {
    // Server
    int http_port = 8080;
    int ws_port = 8081;
    bool debug = true;

    // Drone
    int max_count = 6;
    int heartbeat_hz = 2;
    int lost_timeout_sec = 10;
    double arrival_threshold_m = 1.0;
    int assembly_timeout_sec = 60;
    double avoidance_radius_m = 3.0;
    double avoidance_lookahead_sec = 2.0;
    int low_battery_threshold = 20;

    // Port mapping
    std::unordered_map<int, PortMapping> port_map;  // slot → PortMapping

    // Jetson
    std::string jetson_host = "192.168.30.104";

    // Storage
    std::string storage_path = "./data/drones.json";

    // Log
    std::string log_level = "info";
    std::string log_file;
};

/// 从 config.yaml 加载配置
AppConfig LoadConfig(const std::string& path = "config.yaml");

/// 验证配置合法性（抛出异常则配置无效）
void ValidateConfig(const AppConfig& cfg);
