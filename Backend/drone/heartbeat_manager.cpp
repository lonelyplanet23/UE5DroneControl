#include "heartbeat_manager.h"
#include <spdlog/spdlog.h>
#include <chrono>
#include <thread>

HeartbeatManager::HeartbeatManager(UdpSender& sender)
    : sender_(sender)
{
}

HeartbeatManager::~HeartbeatManager()
{
    StopAll();
}

// ========================================================
// Start 对外接口：不需要锁 → 内部 StopInternal 无锁
// ========================================================
void HeartbeatManager::Start(int drone_id, const std::string& jetson_ip,
                              int send_port, CommandProvider cmd_provider)
{
    // 先停止已有的（在外面 stop，不持有锁，避免死锁）
    StopInternal(drone_id);

    auto state = std::make_shared<HeartbeatState>();
    state->running     = true;
    state->get_command = std::move(cmd_provider);

    sender_.SetTarget(drone_id, jetson_ip, send_port);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        states_[drone_id] = state;
        threads_[drone_id] = std::make_unique<std::thread>(
            &HeartbeatManager::HeartbeatLoop, this, drone_id);
    }

    spdlog::info("[Heartbeat] Started drone {} -> {}:{}",
                 drone_id, jetson_ip, send_port);
}

void HeartbeatManager::UpdateLastPosition(int drone_id, double x, double y, double z)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = states_.find(drone_id);
    if (it != states_.end()) {
        it->second->last_ned_x.store(x);
        it->second->last_ned_y.store(y);
        it->second->last_ned_z.store(z);
    }
}

void HeartbeatManager::Stop(int drone_id)
{
    std::shared_ptr<HeartbeatState> state;
    std::unique_ptr<std::thread> thread;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        auto state_it = states_.find(drone_id);
        if (state_it != states_.end()) {
            state = state_it->second;
            state->running = false;
            states_.erase(state_it);
        }

        auto thread_it = threads_.find(drone_id);
        if (thread_it != threads_.end()) {
            thread = std::move(thread_it->second);
            threads_.erase(thread_it);
        }
    }

    // 在锁外 join 线程
    if (thread && thread->joinable()) {
        thread->join();
    }

    spdlog::info("[Heartbeat] Stopped drone {}", drone_id);
}

void HeartbeatManager::StopInternal(int drone_id)
{
    // 仅在内部使用：不锁定 mutex，调用前必须已持有锁或确保单线程
    auto state_it = states_.find(drone_id);
    if (state_it != states_.end()) {
        state_it->second->running = false;
    }

    auto thread_it = threads_.find(drone_id);
    if (thread_it != threads_.end()) {
        if (thread_it->second->joinable()) {
            thread_it->second->join();
        }
        threads_.erase(thread_it);
    }

    states_.erase(drone_id);
}

void HeartbeatManager::StopAll()
{
    // 先标记所有线程停止
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [id, state] : states_) {
            state->running = false;
        }
    }

    // 逐一 join（锁外）
    // join 后 threads_ 中可能还持有已完成线程的项
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [id, thread] : threads_) {
            if (thread->joinable()) thread->join();
        }
        states_.clear();
        threads_.clear();
    }

    spdlog::info("[Heartbeat] All stopped");
}

HeartbeatStats HeartbeatManager::GetStats(int drone_id) const
{
    HeartbeatStats stats;
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = states_.find(drone_id);
    if (it == states_.end() || !it->second) {
        return stats;
    }

    const auto& state = *it->second;
    stats.running = state.running.load();
    stats.last_sent_time = state.last_sent_time.load();
    stats.sent_count = state.sent_count.load();
    stats.last_ned_x = state.last_ned_x.load();
    stats.last_ned_y = state.last_ned_y.load();
    stats.last_ned_z = state.last_ned_z.load();
    return stats;
}

// ========================================================
// 心跳循环（核心修复：#1 指令队列消费）
// ========================================================
void HeartbeatManager::HeartbeatLoop(int drone_id)
{
    std::shared_ptr<HeartbeatState> state;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = states_.find(drone_id);
        if (it == states_.end()) return;
        state = it->second;
    }

    while (state->running) {
        auto now = std::chrono::system_clock::now();
        double ts = static_cast<double>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                now.time_since_epoch()).count()) / 1000000.0;

        DroneControlPacket pkt{};
        pkt.timestamp = ts;

        // [修复 #1]: 先检查指令队列是否有待发送指令
        bool has_command = false;
        if (state->get_command) {
            has_command = state->get_command(pkt);
        }

        if (has_command) {
            // 从队列取到的指令已包含目标位置和 mode=1
            pkt.timestamp = ts;  // 使用当前时间戳
        } else {
            // 队列为空 → 以 Mode=0 悬停维持最后位置
            pkt.x    = static_cast<float>(state->last_ned_x.load());
            pkt.y    = static_cast<float>(state->last_ned_y.load());
            pkt.z    = static_cast<float>(state->last_ned_z.load());
            pkt.mode = 0;
        }

        sender_.Send(drone_id, pkt);
        state->last_sent_time.store(ts);
        state->sent_count.fetch_add(1);

        // 2Hz 心跳
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}
