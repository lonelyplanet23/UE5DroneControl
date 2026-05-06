#pragma once

#include "core/types.h"
#include "communication/udp_sender.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

using CommandProvider = std::function<bool(DroneControlPacket& cmd)>;

struct HeartbeatState {
    std::atomic<bool> running{false};
    std::atomic<double> last_ned_x{0.0};
    std::atomic<double> last_ned_y{0.0};
    std::atomic<double> last_ned_z{-1.0};
    std::atomic<double> last_sent_time{0.0};
    std::atomic<uint64_t> sent_count{0};
    CommandProvider get_command;
};

struct HeartbeatStats {
    bool running = false;
    double last_sent_time = 0.0;
    uint64_t sent_count = 0;
    double last_ned_x = 0.0;
    double last_ned_y = 0.0;
    double last_ned_z = -1.0;
};

class HeartbeatManager {
public:
    explicit HeartbeatManager(UdpSender& sender);
    ~HeartbeatManager();

    void Start(int drone_id, const std::string& jetson_ip, int send_port,
               CommandProvider cmd_provider = nullptr);

    void UpdateLastPosition(int drone_id, double x, double y, double z);
    void Stop(int drone_id);
    void StopAll();
    HeartbeatStats GetStats(int drone_id) const;

private:
    void StopInternal(int drone_id);
    void HeartbeatLoop(int drone_id);

    UdpSender& sender_;
    std::unordered_map<int, std::shared_ptr<HeartbeatState>> states_;
    std::unordered_map<int, std::unique_ptr<std::thread>> threads_;
    mutable std::mutex mutex_;
};
