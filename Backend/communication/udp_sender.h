#pragma once

#include "core/types.h"
#include <boost/asio.hpp>
#include <string>
#include <memory>

/// UDP 24 字节控制包发送器
///
/// 向 Jetson 发送 24 字节小端控制包
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

    /// 发送控制包
    /// @param drone_id  目标无人机 ID
    /// @param packet    24 字节控制包
    /// @return true 如果发送成功
    bool Send(int drone_id, const DroneControlPacket& packet);

    /// 简便方法：发送移动指令
    void SendMove(int drone_id, double ned_x, double ned_y, double ned_z);

    /// 简便方法：发送悬停指令
    void SendHover(int drone_id);

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

    Target* GetOrCreateTarget(int drone_id);

    boost::asio::io_context& io_context_;
    std::unordered_map<int, std::unique_ptr<Target>> targets_;
};
