#include "udp_receiver.h"
#include <yaml-cpp/yaml.h>
#include <spdlog/spdlog.h>
#include <sstream>

UdpReceiver::UdpReceiver(boost::asio::io_context& io_context)
    : io_context_(io_context)
{
}

UdpReceiver::~UdpReceiver()
{
    Stop();
}

void UdpReceiver::AddPort(int slot, int port, int drone_id)
{
    auto listener = std::make_unique<PortListener>(
        PortListener{slot, port, drone_id,
                     boost::asio::ip::udp::socket(io_context_),
                     boost::asio::ip::udp::endpoint(),
                     std::array<char, 65535>{}});

    listeners_.push_back(std::move(listener));
    spdlog::info("[UdpReceiver] Slot {}: listening port {} for drone {}",
                 slot, port, drone_id);
}

void UdpReceiver::SetCallback(ReceiveCallback cb)
{
    callback_ = std::move(cb);
}

void UdpReceiver::Start()
{
    if (running_) return;
    running_ = true;

    for (auto& listener : listeners_) {
        try {
            listener->socket.open(boost::asio::ip::udp::v4());
            listener->socket.set_option(boost::asio::socket_base::reuse_address(true));
            listener->socket.bind(boost::asio::ip::udp::endpoint(
                boost::asio::ip::address_v4::any(), listener->port));
            spdlog::info("[UdpReceiver] Listening on port {}", listener->port);
            StartReceive(*listener);
        } catch (const std::exception& e) {
            spdlog::error("[UdpReceiver] Failed to bind port {}: {}", listener->port, e.what());
        }
    }
}

void UdpReceiver::Stop()
{
    if (!running_) return;
    running_ = false;

    for (auto& listener : listeners_) {
        try {
            listener->socket.close();
        } catch (...) {}
    }
    spdlog::info("[UdpReceiver] Stopped");
}

void UdpReceiver::StartReceive(PortListener& listener)
{
    if (!running_) return;

    listener.socket.async_receive_from(
        boost::asio::buffer(listener.buffer), listener.remote_endpoint,
        [this, &listener](const boost::system::error_code& error, size_t bytes) {
            HandleReceive(listener, error, bytes);
        });
}

void UdpReceiver::HandleReceive(PortListener& listener,
                                 const boost::system::error_code& error,
                                 size_t bytes_transferred)
{
    if (!running_) return;

    if (!error && bytes_transferred > 0) {
        try {
            // [修复 #3]: yaml-cpp 需要 null-terminated 字符串，不能用 string_view
            std::string yaml_str(listener.buffer.data(), bytes_transferred);
            YAML::Node root = YAML::Load(yaml_str);

            TelemetryData tel{};

            // timestamp (微秒)
            if (auto ts = root["timestamp"]) {
                tel.timestamp = ts.as<uint64_t>(0);
            }

            // position [N, E, D]
            if (auto pos = root["position"]) {
                if (pos.IsSequence() && pos.size() >= 3) {
                    tel.position_ned[0] = pos[0].as<double>(0.0);
                    tel.position_ned[1] = pos[1].as<double>(0.0);
                    tel.position_ned[2] = pos[2].as<double>(0.0);
                }
            }

            // quaternion [w, x, y, z]
            if (auto q = root["q"]) {
                if (q.IsSequence() && q.size() >= 4) {
                    tel.quaternion[0] = q[0].as<double>(1.0);
                    tel.quaternion[1] = q[1].as<double>(0.0);
                    tel.quaternion[2] = q[2].as<double>(0.0);
                    tel.quaternion[3] = q[3].as<double>(0.0);
                }
            }

            // velocity [vN, vE, vD]
            if (auto vel = root["velocity"]) {
                if (vel.IsSequence() && vel.size() >= 3) {
                    tel.velocity[0] = vel[0].as<double>(0.0);
                    tel.velocity[1] = vel[1].as<double>(0.0);
                    tel.velocity[2] = vel[2].as<double>(0.0);
                }
            }

            // angular_velocity
            if (auto av = root["angular_velocity"]) {
                if (av.IsSequence() && av.size() >= 3) {
                    tel.angular_velocity[0] = av[0].as<double>(0.0);
                    tel.angular_velocity[1] = av[1].as<double>(0.0);
                    tel.angular_velocity[2] = av[2].as<double>(0.0);
                }
            }

            // battery
            if (auto bat = root["battery"]) {
                tel.battery = bat.as<int>(-1);
            }

            // GPS
            if (auto gps_lat = root["gps_lat"]) tel.gps_lat = gps_lat.as<double>(0.0);
            if (auto gps_lon = root["gps_lon"]) tel.gps_lon = gps_lon.as<double>(0.0);
            if (auto gps_alt = root["gps_alt"]) tel.gps_alt = gps_alt.as<double>(0.0);
            if (auto gps_fix = root["gps_fix"]) tel.gps_fix = gps_fix.as<bool>(false);

            // local_position [x, y, z]
            if (auto lp = root["local_position"]) {
                if (lp.IsSequence() && lp.size() >= 3) {
                    tel.local_position[0] = lp[0].as<double>(0.0);
                    tel.local_position[1] = lp[1].as<double>(0.0);
                    tel.local_position[2] = lp[2].as<double>(0.0);
                }
            }

            // arming_state / nav_state (vehicle_status_v1)
            if (auto arm = root["arming_state"]) tel.arming_state = arm.as<uint8_t>(0);
            if (auto nav = root["nav_state"])    tel.nav_state    = nav.as<uint8_t>(0);

            // 回调通知
            if (callback_) {
                callback_(listener.slot, tel);
            }

        } catch (const YAML::Exception& e) {
            spdlog::warn("[UdpReceiver] YAML parse error on port {}: {}",
                         listener.port, e.what());
        } catch (const std::exception& e) {
            spdlog::warn("[UdpReceiver] Parse error on port {}: {}",
                         listener.port, e.what());
        }
    }

    // 继续接收下一个包
    StartReceive(listener);
}
