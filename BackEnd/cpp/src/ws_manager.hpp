#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <boost/asio/buffer.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/websocket.hpp>

namespace beast = boost::beast;
namespace net = boost::asio;
using tcp = net::ip::tcp;

struct WsSession {
    beast::websocket::stream<tcp::socket>* ws = nullptr;
    mutable std::mutex write_mutex;
    bool alive = true;

    explicit WsSession(beast::websocket::stream<tcp::socket>* socket) : ws(socket) {}
};

class WsManager {
public:
    std::shared_ptr<WsSession> add(beast::websocket::stream<tcp::socket>* ws) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto session = std::make_shared<WsSession>(ws);
        sessions_[ws] = session;
        return session;
    }

    void remove(beast::websocket::stream<tcp::socket>* ws) {
        std::lock_guard<std::mutex> lock(mutex_);
        sessions_.erase(ws);
    }

    void send(const std::shared_ptr<WsSession>& session, const std::string& message) {
        if (!session || !session->alive || session->ws == nullptr) {
            return;
        }

        std::lock_guard<std::mutex> lock(session->write_mutex);
        try {
            session->ws->text(true);
            session->ws->write(net::buffer(message));
        } catch (...) {
            session->alive = false;
        }
    }

    void broadcast(const std::string& message) {
        const auto snapshot = snapshot_sessions();
        for (const auto& session : snapshot) {
            send(session, message);
        }
        cleanup_dead();
    }

    std::size_t count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return sessions_.size();
    }

private:
    std::vector<std::shared_ptr<WsSession>> snapshot_sessions() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::shared_ptr<WsSession>> snapshot;
        snapshot.reserve(sessions_.size());
        for (const auto& item : sessions_) {
            snapshot.push_back(item.second);
        }
        return snapshot;
    }

    void cleanup_dead() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = sessions_.begin(); it != sessions_.end();) {
            if (!it->second || !it->second->alive) {
                it = sessions_.erase(it);
            } else {
                ++it;
            }
        }
    }

    mutable std::mutex mutex_;
    std::map<void*, std::shared_ptr<WsSession>> sessions_;
};
