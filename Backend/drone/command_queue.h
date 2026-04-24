#pragma once

#include "core/types.h"
#include <queue>
#include <mutex>
#include <condition_variable>
#include <chrono>

/// 每架无人机独立的 FIFO 线程安全指令队列
///
/// - 线程安全的 Push / Pop / Peek
/// - 队列满时丢弃最旧指令（有界队列）
/// - 支持暂停/恢复
class CommandQueue {
public:
    explicit CommandQueue(size_t max_size = 128);

    /// 入队（线程安全）
    /// 队列满时丢弃最旧指令
    void Push(const DroneControlPacket& cmd);

    /// 出队（线程安全，非阻塞）
    /// @return true 如果有指令可取
    bool Pop(DroneControlPacket& cmd);

    /// 查看队首不移除
    bool Peek(DroneControlPacket& cmd) const;

    /// 清空队列
    void Clear();

    /// 当前队列大小
    size_t Size() const;

    /// 暂停/恢复
    void SetPaused(bool paused);
    bool IsPaused() const;

    /// 最大容量
    size_t MaxSize() const { return max_size_; }

private:
    size_t max_size_;
    bool paused_ = false;

    mutable std::mutex mutex_;
    std::queue<DroneControlPacket> queue_;
};
