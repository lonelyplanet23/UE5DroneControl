#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "models.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
#define closesocket close
#endif

inline bool send_control_packet(const std::string& ip, int port, float x, float y, float z, std::uint32_t mode) {
    const SocketHandle socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd == kInvalidSocket) {
        return false;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<std::uint16_t>(port));
    if (inet_pton(AF_INET, ip.c_str(), &address.sin_addr) != 1) {
        closesocket(socket_fd);
        return false;
    }

    std::array<std::uint8_t, 24> buffer{};
    const double timestamp = static_cast<double>(std::time(nullptr));
    std::memcpy(buffer.data() + 0, &timestamp, sizeof(timestamp));
    std::memcpy(buffer.data() + 8, &x, sizeof(x));
    std::memcpy(buffer.data() + 12, &y, sizeof(y));
    std::memcpy(buffer.data() + 16, &z, sizeof(z));
    std::memcpy(buffer.data() + 20, &mode, sizeof(mode));

    const int sent = sendto(
        socket_fd,
        reinterpret_cast<const char*>(buffer.data()),
        static_cast<int>(buffer.size()),
        0,
        reinterpret_cast<const sockaddr*>(&address),
        sizeof(address)
    );
    closesocket(socket_fd);
    return sent == static_cast<int>(buffer.size());
}

inline SocketHandle open_udp_listener(int port, int timeout_ms, std::string* error) {
    const SocketHandle socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd == kInvalidSocket) {
        if (error != nullptr) {
            *error = "socket() failed";
        }
        return kInvalidSocket;
    }

    int reuse = 1;
    setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

#ifdef _WIN32
    DWORD timeout = static_cast<DWORD>(timeout_ms);
    setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
    timeval timeout{};
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#endif

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<std::uint16_t>(port));
    address.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(socket_fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        if (error != nullptr) {
            *error = "bind() failed on port " + std::to_string(port);
        }
        closesocket(socket_fd);
        return kInvalidSocket;
    }

    return socket_fd;
}

inline bool receive_udp_packet(SocketHandle socket_fd, std::string& payload) {
    std::array<char, 4096> buffer{};
    sockaddr_in peer{};
#ifdef _WIN32
    int peer_len = sizeof(peer);
#else
    socklen_t peer_len = sizeof(peer);
#endif

    const int received = recvfrom(
        socket_fd,
        buffer.data(),
        static_cast<int>(buffer.size()),
        0,
        reinterpret_cast<sockaddr*>(&peer),
        &peer_len
    );

    if (received <= 0) {
        return false;
    }

    payload.assign(buffer.data(), buffer.data() + received);
    return true;
}

inline void close_udp_listener(SocketHandle socket_fd) {
    if (socket_fd != kInvalidSocket) {
        closesocket(socket_fd);
    }
}

inline std::optional<std::vector<double>> parse_yaml_number_list(const std::string& raw) {
    const std::size_t left = raw.find('[');
    const std::size_t right = raw.find(']');
    if (left == std::string::npos || right == std::string::npos || left >= right) {
        return std::nullopt;
    }

    std::string inner = raw.substr(left + 1, right - left - 1);
    std::stringstream stream(inner);
    std::string item;
    std::vector<double> values;
    while (std::getline(stream, item, ',')) {
        try {
            const std::size_t hash = item.find('#');
            const std::string cleaned = hash == std::string::npos ? item : item.substr(0, hash);
            values.push_back(std::stod(cleaned));
        } catch (...) {
            return std::nullopt;
        }
    }
    return values;
}

inline bool parse_yaml_telemetry(const std::string& payload, TelemetryFrame& frame, std::string* error) {
    bool has_position = false;
    bool has_q = false;
    bool has_velocity = false;

    std::stringstream stream(payload);
    std::string line;
    while (std::getline(stream, line)) {
        const std::size_t hash = line.find('#');
        if (hash != std::string::npos) {
            line = line.substr(0, hash);
        }

        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }

        auto trim = [](std::string value) {
            std::size_t begin = 0;
            while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) {
                ++begin;
            }
            std::size_t end = value.size();
            while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
                --end;
            }
            return value.substr(begin, end - begin);
        };

        const std::string key = trim(line.substr(0, colon));
        const std::string value = trim(line.substr(colon + 1));

        try {
            if (key == "timestamp") {
                frame.timestamp_us = static_cast<std::uint64_t>(std::stoull(value));
            } else if (key == "position") {
                const auto values = parse_yaml_number_list(value);
                if (!values || values->size() != 3) {
                    throw std::runtime_error("invalid position");
                }
                frame.position = {(*values)[0], (*values)[1], (*values)[2]};
                has_position = true;
            } else if (key == "q") {
                const auto values = parse_yaml_number_list(value);
                if (!values || values->size() != 4) {
                    throw std::runtime_error("invalid q");
                }
                frame.q = {(*values)[0], (*values)[1], (*values)[2], (*values)[3]};
                has_q = true;
            } else if (key == "velocity") {
                const auto values = parse_yaml_number_list(value);
                if (!values || values->size() != 3) {
                    throw std::runtime_error("invalid velocity");
                }
                frame.velocity = {(*values)[0], (*values)[1], (*values)[2]};
                has_velocity = true;
            } else if (key == "angular_velocity") {
                const auto values = parse_yaml_number_list(value);
                if (!values || values->size() != 3) {
                    throw std::runtime_error("invalid angular_velocity");
                }
                frame.angular_velocity = {(*values)[0], (*values)[1], (*values)[2]};
            } else if (key == "battery") {
                frame.battery = std::stoi(value);
            } else if (key == "gps_lat") {
                if (!frame.anchor.has_value()) {
                    frame.anchor = GeoAnchor{};
                }
                frame.anchor->available = true;
                frame.anchor->gps_lat = std::stod(value);
            } else if (key == "gps_lon") {
                if (!frame.anchor.has_value()) {
                    frame.anchor = GeoAnchor{};
                }
                frame.anchor->available = true;
                frame.anchor->gps_lon = std::stod(value);
            } else if (key == "gps_alt") {
                if (!frame.anchor.has_value()) {
                    frame.anchor = GeoAnchor{};
                }
                frame.anchor->available = true;
                frame.anchor->gps_alt = std::stod(value);
            }
        } catch (...) {
            if (error != nullptr) {
                *error = "failed to parse field: " + key;
            }
            return false;
        }
    }

    if (!has_position || !has_q || !has_velocity) {
        if (error != nullptr) {
            *error = "missing required telemetry fields";
        }
        return false;
    }

    return true;
}
