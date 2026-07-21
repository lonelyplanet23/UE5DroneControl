#include "heartbeat_manager.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <chrono>
#include <thread>
#include <cstdint>

HeartbeatManager::HeartbeatManager(UdpSender& sender, int heartbeat_hz,
                                   int command_repeat_count)
    : sender_(sender)
    , heartbeat_interval_ms_(std::max(1, 1000 / std::max(heartbeat_hz, 1)))
    , command_repeat_count_(std::max(3, command_repeat_count))
{
}

HeartbeatManager::~HeartbeatManager()
{
    StopAll();
}

// ========================================================
// Start 对外接口：不需要锁 -> 内部 StopInternal 无锁
// ========================================================
void HeartbeatManager::Start(int drone_id, int slot, const std::string& jetson_ip,
                             int send_port, CommandProvider cmd_provider,
                             double initial_ned_x, double initial_ned_y,
                             double initial_ned_z)
{
    // 先停止已有的（在外面 stop，不持有锁，避免死锁）
    StopInternal(drone_id);

    auto state = std::make_shared<HeartbeatState>();
    state->running     = true;
    state->slot        = slot;
    state->get_command = std::move(cmd_provider);
    // The worker may send immediately after it starts. Holding an airborne
    // vehicle at a guessed (0,0,0) would command the PX4 local origin.
    state->last_ned_x  = initial_ned_x;
    state->last_ned_y  = initial_ned_y;
    state->last_ned_z  = initial_ned_z;

    sender_.SetTarget(drone_id, jetson_ip, send_port);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        states_[drone_id] = state;
        threads_[drone_id] = std::make_unique<std::thread>(
            &HeartbeatManager::HeartbeatLoop, this, drone_id);
    }

    spdlog::info(
        "[Heartbeat] Started drone {} slot {} -> {}:{} interval={}ms repeat={}",
        drone_id, slot, jetson_ip, send_port,
        heartbeat_interval_ms_, command_repeat_count_);
}

void HeartbeatManager::RequestHold(int drone_id, double x, double y, double z)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = states_.find(drone_id);
    if (it != states_.end()) {
        it->second->last_ned_x.store(x);
        it->second->last_ned_y.store(y);
        it->second->last_ned_z.store(z);
        it->second->hold_request_generation.fetch_add(1);
        spdlog::info(
            "[Heartbeat] Hold requested drone {} NED({:.3f},{:.3f},{:.3f})",
            drone_id, x, y, z);
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
    std::shared_ptr<HeartbeatState> state;
    std::unique_ptr<std::thread> thread;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto state_it = states_.find(drone_id);
        if (state_it != states_.end()) {
            state_it->second->running = false;
            state = state_it->second;
            states_.erase(state_it);
        }
        auto thread_it = threads_.find(drone_id);
        if (thread_it != threads_.end()) {
            thread = std::move(thread_it->second);
            threads_.erase(thread_it);
        }
    }

    if (thread && thread->joinable()) {
        thread->join();
    }
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
    stats.send_failed_count = state.send_failed_count.load();
    stats.last_ned_x = state.last_ned_x.load();
    stats.last_ned_y = state.last_ned_y.load();
    stats.last_ned_z = state.last_ned_z.load();
    stats.active_sequence = state.active_sequence.load();
    stats.active_mode = state.active_mode.load();
    stats.repeat_index = state.repeat_index.load();
    stats.repeat_total = state.repeat_total.load();
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

    auto unix_seconds = []() {
        return static_cast<double>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count())
            / 1000000.0;
    };

    auto make_hold_packet = [&]() {
        DroneControlPacket hold{};
        hold.sequence = NextControlSequence();
        hold.timestamp = unix_seconds();
        hold.x = static_cast<float>(state->last_ned_x.load());
        hold.y = static_cast<float>(state->last_ned_y.load());
        hold.z = static_cast<float>(state->last_ned_z.load());
        hold.mode = 0;
        hold.slot = state->slot;
        hold.repeat_total = 0;
        return hold;
    };

    DroneControlPacket hold_packet = make_hold_packet();
    uint32_t hold_repeat_index = 0;
    uint64_t observed_hold_generation = state->hold_request_generation.load();
    bool has_active_move = false;
    DroneControlPacket active_move{};
    uint32_t move_sent_count = 0;

    while (state->running) {
        const auto requested_generation = state->hold_request_generation.load();
        if (requested_generation != observed_hold_generation) {
            observed_hold_generation = requested_generation;
            has_active_move = false;
            move_sent_count = 0;
            hold_repeat_index = 0;
            hold_packet = make_hold_packet();
            spdlog::info(
                "[Heartbeat] Drone {} explicit hold sequence={} "
                "NED({:.3f},{:.3f},{:.3f})",
                drone_id, hold_packet.sequence,
                hold_packet.x, hold_packet.y, hold_packet.z);
        }

        // 一条 move 必须完成固定次数重发后，才消费队列中的下一条。
        if (!has_active_move && state->get_command) {
            DroneControlPacket queued{};
            if (state->get_command(queued)) {
                if (queued.sequence == 0) queued.sequence = NextControlSequence();
                queued.mode = 1;
                queued.slot = state->slot;
                queued.repeat_total = static_cast<uint32_t>(command_repeat_count_);
                active_move = queued;
                has_active_move = true;
                move_sent_count = 0;
                state->last_ned_x.store(queued.x);
                state->last_ned_y.store(queued.y);
                state->last_ned_z.store(queued.z);
                spdlog::info(
                    "[Heartbeat] Drone {} begin move sequence={} repeat={} "
                    "NED({:.3f},{:.3f},{:.3f})",
                    drone_id, queued.sequence, command_repeat_count_,
                    queued.x, queued.y, queued.z);
            }
        }

        DroneControlPacket packet{};
        if (has_active_move) {
            packet = active_move;
            packet.repeat_index = move_sent_count + 1;
            packet.repeat_total = static_cast<uint32_t>(command_repeat_count_);
        } else {
            packet = hold_packet;
            if (hold_repeat_index == UINT32_MAX) hold_repeat_index = 0;
            packet.repeat_index = hold_repeat_index + 1;
            packet.repeat_total = 0; // 持续发送，直到新 move 或显式 hold
        }

        const bool sent = sender_.Send(drone_id, packet);
        state->last_sent_time.store(unix_seconds());
        state->active_sequence.store(packet.sequence);
        state->active_mode.store(packet.mode);
        state->repeat_index.store(packet.repeat_index);
        state->repeat_total.store(packet.repeat_total);

        if (sent) {
            state->sent_count.fetch_add(1);
            if (has_active_move) {
                ++move_sent_count;
                if (move_sent_count >= static_cast<uint32_t>(command_repeat_count_)) {
                    spdlog::info(
                        "[Heartbeat] Drone {} move sequence={} sent {}/{} times; "
                        "same target becomes hold",
                        drone_id, active_move.sequence,
                        move_sent_count, command_repeat_count_);
                    has_active_move = false;
                    move_sent_count = 0;
                    hold_repeat_index = 0;
                    hold_packet = make_hold_packet();
                }
            } else {
                ++hold_repeat_index;
            }
        } else {
            state->send_failed_count.fetch_add(1);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(heartbeat_interval_ms_));
    }
}
