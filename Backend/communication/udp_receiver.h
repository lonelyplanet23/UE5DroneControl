#pragma once

#include "core/types.h"
#include <boost/asio.hpp>
#include <functional>
#include <thread>
#include <memory>
#include <unordered_map>

/// UDP YAML 遥测接收器
///
/// 监听多个端口（每个无人机一个），接收 YAML 格式遥测数据
/// 解析后通过回调通知 DroneManager
class UdpReceiver {
public:
    using ReceiveCallback = std::function<void(int drone_id, const TelemetryData&)>;

    UdpReceiver(boost::asio::io_context& io_context);
    ~UdpReceiver();

    /// 添加监听端口（每个槽位对应一个端口）
    /// @param slot     编号 1~6
    /// @param port     监听端口（如 8888）
    /// @param drone_id 该端口对应的无人机 ID
    void AddPort(int slot, int port, int drone_id);

    /// 设置遥测接收回调
    void SetCallback(ReceiveCallback cb);

    /// 启动所有端口监听
    void Start();

    /// 停止所有端口监听
    void Stop();

    /// 是否正在运行
    bool IsRunning() const { return running_; }

private:
    struct PortListener {
        int slot;
        int port;
        int drone_id;
        boost::asio::ip::udp::socket socket;
        boost::asio::ip::udp::endpoint remote_endpoint;
        std::array<char, 65535> buffer{};
    };

    void StartReceive(PortListener& listener);
    void HandleReceive(PortListener& listener, const boost::system::error_code& error, size_t bytes_transferred);

    boost::asio::io_context& io_context_;
    std::vector<std::unique_ptr<PortListener>> listeners_;
    ReceiveCallback callback_;
    bool running_ = false;
};
