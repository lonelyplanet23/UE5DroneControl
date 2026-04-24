#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>

struct PortMapping {
    int send_port = 0;
    int recv_port = 0;
};

struct Config {
    std::filesystem::path config_path;
    std::filesystem::path base_dir;

    int http_port = 8080;
    int ws_port = 8081;
    bool debug = true;

    int max_count = 6;
    double heartbeat_hz = 2.0;
    int lost_timeout_sec = 10;
    double arrival_threshold_m = 1.0;
    int assembly_timeout_sec = 60;
    double avoidance_radius_m = 3.0;
    double avoidance_lookahead_sec = 2.0;
    int low_battery_threshold = 20;

    std::unordered_map<int, PortMapping> port_map;

    std::string storage_path = "./data/drones.json";
    std::string log_level = "info";
    std::string log_file = "./logs/backend.log";

    int send_port_for_slot(int slot) const {
        auto it = port_map.find(slot);
        return it == port_map.end() ? 0 : it->second.send_port;
    }

    int recv_port_for_slot(int slot) const {
        auto it = port_map.find(slot);
        return it == port_map.end() ? 0 : it->second.recv_port;
    }
};

inline std::string trim_copy(const std::string& value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) {
        ++begin;
    }

    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }

    return value.substr(begin, end - begin);
}

inline std::string unquote_copy(const std::string& value) {
    if (value.size() >= 2) {
        const char first = value.front();
        const char last = value.back();
        if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
            return value.substr(1, value.size() - 2);
        }
    }
    return value;
}

inline std::string strip_comment_copy(const std::string& value) {
    bool in_single = false;
    bool in_double = false;
    for (std::size_t i = 0; i < value.size(); ++i) {
        const char ch = value[i];
        if (ch == '\'' && !in_double) {
            in_single = !in_single;
        } else if (ch == '"' && !in_single) {
            in_double = !in_double;
        } else if (ch == '#' && !in_single && !in_double) {
            return trim_copy(value.substr(0, i));
        }
    }
    return trim_copy(value);
}

inline int indentation_of(const std::string& value) {
    int count = 0;
    while (count < static_cast<int>(value.size()) && value[count] == ' ') {
        ++count;
    }
    return count;
}

inline bool parse_bool_scalar(const std::string& value, bool fallback) {
    const std::string v = trim_copy(unquote_copy(value));
    if (v == "true" || v == "True" || v == "TRUE" || v == "1") {
        return true;
    }
    if (v == "false" || v == "False" || v == "FALSE" || v == "0") {
        return false;
    }
    return fallback;
}

inline int parse_int_scalar(const std::string& value, int fallback) {
    try {
        return std::stoi(trim_copy(unquote_copy(value)));
    } catch (...) {
        return fallback;
    }
}

inline double parse_double_scalar(const std::string& value, double fallback) {
    try {
        return std::stod(trim_copy(unquote_copy(value)));
    } catch (...) {
        return fallback;
    }
}

inline void apply_default_port_map(Config& cfg) {
    if (!cfg.port_map.empty()) {
        return;
    }

    cfg.port_map = {
        {1, {8889, 8888}},
        {2, {8891, 8890}},
        {3, {8893, 8892}},
        {4, {8895, 8894}},
        {5, {8897, 8896}},
        {6, {8899, 8898}},
    };
}

inline std::string resolve_config_relative_path(const Config& cfg, const std::string& raw_path) {
    if (raw_path.empty()) {
        return raw_path;
    }

    std::filesystem::path path(raw_path);
    if (path.is_absolute()) {
        return path.lexically_normal().string();
    }

    if (cfg.base_dir.empty()) {
        return path.lexically_normal().string();
    }

    return (cfg.base_dir / path).lexically_normal().string();
}

inline Config load_config(const std::string& path) {
    Config cfg;
    cfg.config_path = std::filesystem::absolute(path);
    cfg.base_dir = cfg.config_path.parent_path();
    apply_default_port_map(cfg);

    std::ifstream file(path);
    if (!file.is_open()) {
        cfg.storage_path = resolve_config_relative_path(cfg, cfg.storage_path);
        cfg.log_file = resolve_config_relative_path(cfg, cfg.log_file);
        return cfg;
    }

    std::string section;
    int current_slot = 0;
    std::string line;
    while (std::getline(file, line)) {
        const int indent = indentation_of(line);
        const std::string cleaned = strip_comment_copy(line);
        if (cleaned.empty()) {
            continue;
        }

        if (indent == 0 && cleaned.back() == ':') {
            section = cleaned.substr(0, cleaned.size() - 1);
            current_slot = 0;
            continue;
        }

        if (section == "port_map" && indent == 2 && cleaned.back() == ':') {
            current_slot = parse_int_scalar(cleaned.substr(0, cleaned.size() - 1), 0);
            if (current_slot > 0) {
                cfg.port_map[current_slot];
            }
            continue;
        }

        const std::size_t colon = cleaned.find(':');
        if (colon == std::string::npos) {
            continue;
        }

        const std::string key = trim_copy(cleaned.substr(0, colon));
        const std::string value = trim_copy(cleaned.substr(colon + 1));

        if (section == "server") {
            if (key == "http_port") {
                cfg.http_port = parse_int_scalar(value, cfg.http_port);
            } else if (key == "ws_port") {
                cfg.ws_port = parse_int_scalar(value, cfg.ws_port);
            } else if (key == "debug") {
                cfg.debug = parse_bool_scalar(value, cfg.debug);
            }
        } else if (section == "drone") {
            if (key == "max_count") {
                cfg.max_count = parse_int_scalar(value, cfg.max_count);
            } else if (key == "heartbeat_hz") {
                cfg.heartbeat_hz = parse_double_scalar(value, cfg.heartbeat_hz);
            } else if (key == "lost_timeout_sec") {
                cfg.lost_timeout_sec = parse_int_scalar(value, cfg.lost_timeout_sec);
            } else if (key == "arrival_threshold_m") {
                cfg.arrival_threshold_m = parse_double_scalar(value, cfg.arrival_threshold_m);
            } else if (key == "assembly_timeout_sec") {
                cfg.assembly_timeout_sec = parse_int_scalar(value, cfg.assembly_timeout_sec);
            } else if (key == "avoidance_radius_m") {
                cfg.avoidance_radius_m = parse_double_scalar(value, cfg.avoidance_radius_m);
            } else if (key == "avoidance_lookahead_sec") {
                cfg.avoidance_lookahead_sec = parse_double_scalar(value, cfg.avoidance_lookahead_sec);
            } else if (key == "low_battery_threshold") {
                cfg.low_battery_threshold = parse_int_scalar(value, cfg.low_battery_threshold);
            }
        } else if (section == "port_map" && current_slot > 0) {
            if (key == "send_port") {
                cfg.port_map[current_slot].send_port = parse_int_scalar(value, cfg.port_map[current_slot].send_port);
            } else if (key == "recv_port") {
                cfg.port_map[current_slot].recv_port = parse_int_scalar(value, cfg.port_map[current_slot].recv_port);
            }
        } else if (section == "storage") {
            if (key == "path") {
                cfg.storage_path = unquote_copy(value);
            }
        } else if (section == "log") {
            if (key == "level") {
                cfg.log_level = unquote_copy(value);
            } else if (key == "file") {
                cfg.log_file = unquote_copy(value);
            }
        }
    }

    cfg.max_count = std::max(1, std::min(cfg.max_count, 6));
    cfg.heartbeat_hz = std::max(2.0, cfg.heartbeat_hz);
    cfg.lost_timeout_sec = std::max(1, cfg.lost_timeout_sec);
    cfg.arrival_threshold_m = std::max(0.01, cfg.arrival_threshold_m);
    cfg.assembly_timeout_sec = std::max(1, cfg.assembly_timeout_sec);
    cfg.low_battery_threshold = std::max(0, std::min(cfg.low_battery_threshold, 100));

    apply_default_port_map(cfg);
    cfg.storage_path = resolve_config_relative_path(cfg, cfg.storage_path);
    cfg.log_file = resolve_config_relative_path(cfg, cfg.log_file);

    return cfg;
}
