#include "command_queue.h"
#include <spdlog/spdlog.h>

CommandQueue::CommandQueue(size_t max_size)
    : max_size_(max_size)
{
}

void CommandQueue::Push(const DroneControlPacket& cmd)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (queue_.size() >= max_size_) {
        // 队列满，丢弃最旧指令
        queue_.pop();
        spdlog::warn("[CommandQueue] Queue full, dropping oldest command");
    }

    queue_.push(cmd);
}

bool CommandQueue::Pop(DroneControlPacket& cmd)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (queue_.empty() || paused_) {
        return false;
    }

    cmd = queue_.front();
    queue_.pop();
    return true;
}

bool CommandQueue::Peek(DroneControlPacket& cmd) const
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (queue_.empty()) {
        return false;
    }

    cmd = queue_.front();
    return true;
}

void CommandQueue::Clear()
{
    std::lock_guard<std::mutex> lock(mutex_);
    while (!queue_.empty()) queue_.pop();
}

size_t CommandQueue::Size() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

std::vector<DroneControlPacket> CommandQueue::Snapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::queue<DroneControlPacket> copy = queue_;
    std::vector<DroneControlPacket> result;
    result.reserve(copy.size());
    while (!copy.empty()) {
        result.push_back(copy.front());
        copy.pop();
    }
    return result;
}

void CommandQueue::SetPaused(bool paused)
{
    std::lock_guard<std::mutex> lock(mutex_);
    paused_ = paused;
    spdlog::info("[CommandQueue] {}", paused ? "Paused" : "Resumed");
}

bool CommandQueue::IsPaused() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return paused_;
}
