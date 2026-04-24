#include "udp_sender.h"
#include <spdlog/spdlog.h>
#include <chrono>

UdpSender::UdpSender(boost::asio::io_context& io_context)
    : io_context_(io_context)
{
}

UdpSender::~UdpSender()
{
    spdlog::info("[UdpSender] Destroyed");
}

void UdpSender::SetTarget(int drone_id, const std::string& host, int port)
{
    auto target = std::make_unique<Target>(io_context_);
    target->host = host;
    target->port = port;
    target->endpoint = boost::asio::ip::udp::endpoint(
        boost::asio::ip::address::from_string(host), port);
    target->initialized = true;

    targets_[drone_id] = std::move(target);
    spdlog::info("[UdpSender] Drone {} target set: {}:{}", drone_id, host, port);
}

bool UdpSender::Send(int drone_id, const DroneControlPacket& packet)
{
    auto target = GetOrCreateTarget(drone_id);
    if (!target || !target->initialized) return false;

    try {
        target->socket.send_to(
            boost::asio::buffer(&packet, sizeof(DroneControlPacket)),
            target->endpoint);
        return true;
    } catch (const std::exception& e) {
        spdlog::error("[UdpSender] Send to drone {} failed: {}", drone_id, e.what());
        return false;
    }
}

void UdpSender::SendMove(int drone_id, double ned_x, double ned_y, double ned_z)
{
    DroneControlPacket pkt;
    pkt.timestamp = static_cast<double>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()) / 1000000.0;
    pkt.x = static_cast<float>(ned_x);
    pkt.y = static_cast<float>(ned_y);
    pkt.z = static_cast<float>(ned_z);
    pkt.mode = 1;
    Send(drone_id, pkt);
}

void UdpSender::SendHover(int drone_id)
{
    DroneControlPacket pkt{};
    pkt.timestamp = static_cast<double>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()) / 1000000.0;
    pkt.mode = 0;
    Send(drone_id, pkt);
}

UdpSender::Target* UdpSender::GetOrCreateTarget(int drone_id)
{
    auto it = targets_.find(drone_id);
    if (it != targets_.end()) {
        return it->second.get();
    }
    return nullptr;
}
