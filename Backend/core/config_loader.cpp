#include "config_loader.h"
#include <yaml-cpp/yaml.h>
#include <spdlog/spdlog.h>

AppConfig LoadConfig(const std::string& path)
{
    AppConfig cfg;

    try {
        YAML::Node root = YAML::LoadFile(path);

        // --- server ---
        if (auto s = root["server"]) {
            cfg.http_port  = s["http_port"].as<int>(cfg.http_port);
            cfg.ws_port    = s["ws_port"].as<int>(cfg.ws_port);
            cfg.debug      = s["debug"].as<bool>(cfg.debug);
        }

        // --- drone ---
        if (auto d = root["drone"]) {
            cfg.max_count               = d["max_count"].as<int>(cfg.max_count);
            cfg.heartbeat_hz            = d["heartbeat_hz"].as<int>(cfg.heartbeat_hz);
            cfg.lost_timeout_sec        = d["lost_timeout_sec"].as<int>(cfg.lost_timeout_sec);
            cfg.arrival_threshold_m     = d["arrival_threshold_m"].as<double>(cfg.arrival_threshold_m);
            cfg.assembly_timeout_sec    = d["assembly_timeout_sec"].as<int>(cfg.assembly_timeout_sec);
            cfg.avoidance_radius_m      = d["avoidance_radius_m"].as<double>(cfg.avoidance_radius_m);
            cfg.avoidance_lookahead_sec = d["avoidance_lookahead_sec"].as<double>(cfg.avoidance_lookahead_sec);
            cfg.low_battery_threshold   = d["low_battery_threshold"].as<int>(cfg.low_battery_threshold);
        }

        // --- port_map ---
        if (auto pm = root["port_map"]) {
            for (const auto& kv : pm) {
                int slot = kv.first.as<int>();
                PortMapping mapping;
                mapping.slot = slot;
                mapping.send_port       = kv.second["send_port"].as<int>();
                mapping.recv_port       = kv.second["recv_port"].as<int>();
                mapping.ros_topic_prefix = kv.second["ros_topic_prefix"].as<std::string>();
                cfg.port_map[slot] = mapping;
            }
        }

        // --- jetson ---
        if (auto j = root["jetson"]) {
            cfg.jetson_host = j["host"].as<std::string>(cfg.jetson_host);
        }

        // --- storage ---
        if (auto st = root["storage"]) {
            cfg.storage_path = st["path"].as<std::string>(cfg.storage_path);
        }

        // --- log ---
        if (auto l = root["log"]) {
            cfg.log_level = l["level"].as<std::string>(cfg.log_level);
            cfg.log_file  = l["file"].as<std::string>(cfg.log_file);
        }

        spdlog::info("Config loaded from '{}': {} ports, {} drones max",
                     path, cfg.port_map.size(), cfg.max_count);

    } catch (const std::exception& e) {
        spdlog::error("Failed to load config '{}': {}", path, e.what());
        throw;
    }

    return cfg;
}

void ValidateConfig(const AppConfig& cfg)
{
    if (cfg.heartbeat_hz < 2) {
        throw std::invalid_argument(
            "heartbeat_hz must be >= 2 (PX4 offboard requirement)");
    }
    if (cfg.lost_timeout_sec <= 0) {
        throw std::invalid_argument(
            "lost_timeout_sec must be > 0");
    }
    if (cfg.assembly_timeout_sec <= 0) {
        throw std::invalid_argument(
            "assembly_timeout_sec must be > 0");
    }
    if (cfg.max_count < 1 || cfg.max_count > 6) {
        throw std::invalid_argument(
            "max_count must be 1~6");
    }

    // 端口唯一性检查
    std::unordered_set<int> send_ports, recv_ports;
    for (const auto& [slot, mapping] : cfg.port_map) {
        if (mapping.send_port < 1024 || mapping.send_port > 65535) {
            throw std::invalid_argument(
                "send_port " + std::to_string(mapping.send_port) + " out of range");
        }
        if (mapping.recv_port < 1024 || mapping.recv_port > 65535) {
            throw std::invalid_argument(
                "recv_port " + std::to_string(mapping.recv_port) + " out of range");
        }
        if (!send_ports.insert(mapping.send_port).second) {
            throw std::invalid_argument(
                "duplicate send_port " + std::to_string(mapping.send_port));
        }
        if (!recv_ports.insert(mapping.recv_port).second) {
            throw std::invalid_argument(
                "duplicate recv_port " + std::to_string(mapping.recv_port));
        }
    }

    spdlog::info("Config validation passed ({} ports, {} max drones)",
                 cfg.port_map.size(), cfg.max_count);
}
