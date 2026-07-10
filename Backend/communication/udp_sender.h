#pragma once

#include "core/types.h"
#include <boost/asio.hpp>
#include <mutex>
#include <string>
#include <memory>
#include <unordered_map>

/// UDP JSON 控制指令发送器
///
/// 向 Jetson 发送带 session/command/sequence/repeat 元数据的 JSON 数据报。
/// 支持同时发送到多个无人机
class UdpSender {
public:
    UdpSender(boost::asio::io_context& io_context);
    ~UdpSender();

    /// 设置无人机的发送目标
    /// @param drone_id  无人机 ID
    /// @param host      目标 IP（Jetson IP）
    /// @param port      目标端口（如 8889）
    void SetTarget(int drone_id, const std::string& host, int port);

    /// 发送一个 JSON 控制数据报
    /// @param drone_id  目标无人机 ID
    /// @param packet    内部 NED 控制指令（函数内序列化为 JSON）
    /// @return true 如果发送成功
    bool Send(int drone_id, const DroneControlPacket& packet);

    const std::string& SessionId() const { return session_id_; }

private:
    struct Target {
        std::string host;
        int port = 0;
        boost::asio::ip::udp::endpoint endpoint;
        boost::asio::ip::udp::socket socket;
        bool initialized = false;

        Target(boost::asio::io_context& io)
            : socket(io, boost::asio::ip::udp::v4()) {}
    };

    boost::asio::io_context& io_context_;
    std::string session_id_;
    mutable std::mutex targets_mutex_;
    std::unordered_map<int, std::unique_ptr<Target>> targets_;
};
