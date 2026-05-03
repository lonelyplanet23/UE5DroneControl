#define _WIN32_WINNT 0x0601

#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/json.hpp>

#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "config.hpp"
#include "registry.hpp"
#include "ws_manager.hpp"

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = net::ip::tcp;

static BackendService* g_backend = nullptr;
static WsManager* g_ws_manager = nullptr;
static Logger* g_logger = nullptr;

static std::string json_error(const std::string& detail) {
    return json_stringify(boost::json::object{{"detail", detail}});
}

static bool starts_with(const std::string& value, const std::string& prefix) {
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

static bool path_match(const std::string& path, const std::string& prefix, const std::string& suffix, std::string& id_out) {
    if (!starts_with(path, prefix)) {
        return false;
    }

    std::string rest = path.substr(prefix.size());
    if (suffix.empty()) {
        if (rest.empty() || rest.find('/') != std::string::npos) {
            return false;
        }
        id_out = rest;
        return true;
    }

    const auto position = rest.find(suffix);
    if (position == std::string::npos) {
        return false;
    }

    id_out = rest.substr(0, position);
    return !id_out.empty() && rest.substr(position) == suffix;
}

static http::response<http::string_body> make_response(
    const http::request<http::string_body>& request,
    int status_code,
    const std::string& body,
    const std::string& content_type = "application/json"
) {
    http::response<http::string_body> response{static_cast<http::status>(status_code), request.version()};
    response.set(http::field::server, "UE5DroneControl-Backend/0.2");
    response.set(http::field::content_type, content_type);
    response.set(http::field::access_control_allow_origin, "*");
    response.set(http::field::access_control_allow_methods, "GET,POST,PUT,DELETE,OPTIONS");
    response.set(http::field::access_control_allow_headers, "Content-Type");
    response.keep_alive(request.keep_alive());
    response.body() = body;
    response.prepare_payload();
    return response;
}

static boost::json::value parse_request_body(const http::request<http::string_body>& request) {
    if (request.body().empty()) {
        return boost::json::object();
    }
    try {
        return boost::json::parse(request.body());
    } catch (...) {
        throw ApiError(400, "invalid JSON body");
    }
}

static http::response<http::string_body> handle_http(http::request<http::string_body>& request) {
    try {
        std::string method = std::string(request.method_string());
        std::string path = std::string(request.target());
        const auto query_position = path.find('?');
        if (query_position != std::string::npos) {
            path = path.substr(0, query_position);
        }

        if (method == "OPTIONS") {
            return make_response(request, 204, "");
        }

        const boost::json::value body = parse_request_body(request);
        std::string id;

        if (method == "GET" && path == "/") {
            return make_response(request, 200, json_stringify(boost::json::object{
                {"status", "ok"},
                {"http_port", g_backend->config().http_port},
                {"ws_port", g_backend->config().ws_port},
                {"debug", g_backend->config().debug},
            }));
        }

        if (method == "GET" && path == "/api/drones") {
            return make_response(request, 200, json_stringify(g_backend->list_drones()));
        }

        if (method == "POST" && path == "/api/drones") {
            return make_response(request, 201, json_stringify(g_backend->register_drone(require_object(body, "body"))));
        }

        if (method == "PUT" && path_match(path, "/api/drones/", "", id)) {
            return make_response(request, 200, json_stringify(g_backend->update_drone(id, require_object(body, "body"))));
        }

        if (method == "DELETE" && path_match(path, "/api/drones/", "", id)) {
            return make_response(request, 200, json_stringify(g_backend->delete_drone(id)));
        }

        if (method == "GET" && path_match(path, "/api/drones/", "/anchor", id)) {
            return make_response(request, 200, json_stringify(g_backend->get_anchor(id)));
        }

        if (method == "POST" && path == "/api/arrays") {
            return make_response(request, 201, json_stringify(g_backend->create_array_from_http(require_object(body, "body"))));
        }

        if (method == "POST" && path_match(path, "/api/arrays/", "/stop", id)) {
            return make_response(request, 200, json_stringify(g_backend->stop_array(id)));
        }

        if (!g_backend->config().debug) {
            throw ApiError(404, "not found");
        }

        if (method == "GET" && path_match(path, "/api/debug/drone/", "/state", id)) {
            return make_response(request, 200, json_stringify(g_backend->debug_drone_state(id)));
        }

        if (method == "GET" && path_match(path, "/api/debug/drone/", "/queue", id)) {
            return make_response(request, 200, json_stringify(g_backend->debug_drone_queue(id)));
        }

        if (method == "GET" && path_match(path, "/api/debug/heartbeat/", "", id)) {
            return make_response(request, 200, json_stringify(g_backend->debug_heartbeat(id)));
        }

        if (method == "POST" && path_match(path, "/api/debug/drone/", "/inject", id)) {
            return make_response(request, 200, json_stringify(g_backend->inject_telemetry(id, require_object(body, "body"))));
        }

        if (method == "POST" && path_match(path, "/api/debug/cmd/", "/move", id)) {
            return make_response(request, 200, json_stringify(g_backend->debug_move(id, require_object(body, "body"))));
        }

        if (method == "POST" && path_match(path, "/api/debug/cmd/", "/pause", id)) {
            return make_response(request, 200, json_stringify(g_backend->debug_pause(id, true)));
        }

        if (method == "POST" && path_match(path, "/api/debug/cmd/", "/resume", id)) {
            return make_response(request, 200, json_stringify(g_backend->debug_pause(id, false)));
        }

        if (method == "POST" && path_match(path, "/api/debug/cmd/", "/array", id)) {
            return make_response(request, 200, json_stringify(g_backend->debug_single_array(id, require_object(body, "body"))));
        }

        if (method == "POST" && path_match(path, "/api/debug/cmd/", "/target", id)) {
            return make_response(request, 200, json_stringify(g_backend->debug_target(id, require_object(body, "body"))));
        }

        if (method == "POST" && path == "/api/debug/cmd/batch/array") {
            return make_response(request, 200, json_stringify(g_backend->debug_batch_array(require_array(body, "body"))));
        }

        if (method == "GET" && path_match(path, "/api/debug/arrays/", "/state", id)) {
            return make_response(request, 200, json_stringify(g_backend->debug_array_state(id)));
        }

        throw ApiError(404, "not found");
    } catch (const ApiError& error) {
        return make_response(request, error.status_code, json_error(error.what()));
    } catch (const std::exception& error) {
        if (g_logger != nullptr) {
            g_logger->error(std::string("HTTP handler exception: ") + error.what());
        }
        return make_response(request, 500, json_error("internal server error"));
    }
}

static void send_ws_error(const std::shared_ptr<WsSession>& session, int code, const std::string& message) {
    if (g_ws_manager == nullptr) {
        return;
    }
    g_ws_manager->send(session, json_stringify(boost::json::object{
        {"type", "error"},
        {"code", code},
        {"message", message},
    }));
}

static void run_ws_session(tcp::socket socket, http::request<http::string_body> upgrade_request) {
    websocket::stream<tcp::socket> ws(std::move(socket));
    try {
        ws.set_option(websocket::stream_base::timeout::suggested(beast::role_type::server));
        ws.set_option(websocket::stream_base::decorator([](websocket::response_type& response) {
            response.set(http::field::server, "UE5DroneControl-Backend/0.2");
        }));
        ws.accept(upgrade_request);
    } catch (const std::exception& error) {
        if (g_logger != nullptr) {
            g_logger->warn(std::string("WebSocket accept failed: ") + error.what());
        }
        return;
    }

    auto session = g_ws_manager->add(&ws);
    beast::flat_buffer buffer;

    while (true) {
        try {
            buffer.clear();
            ws.read(buffer);
            const std::string payload = beast::buffers_to_string(buffer.data());

            boost::json::value message;
            try {
                message = boost::json::parse(payload);
            } catch (...) {
                send_ws_error(session, 400, "invalid JSON payload");
                continue;
            }

            if (!message.is_object()) {
                send_ws_error(session, 400, "websocket payload must be an object");
                continue;
            }

            try {
                g_backend->handle_ws_command(message.as_object());
            } catch (const ApiError& error) {
                send_ws_error(session, error.status_code, error.what());
            }
        } catch (const std::exception& error) {
            if (g_logger != nullptr) {
                g_logger->info(std::string("WebSocket session closed: ") + error.what());
            }
            break;
        }
    }

    session->alive = false;
    g_ws_manager->remove(&ws);
}

static void handle_http_connection(tcp::socket socket) {
    beast::flat_buffer buffer;
    http::request<http::string_body> request;
    try {
        http::read(socket, buffer, request);
        auto response = handle_http(request);
        http::write(socket, response);
    } catch (const std::exception& error) {
        if (g_logger != nullptr) {
            g_logger->warn(std::string("HTTP connection error: ") + error.what());
        }
    }
}

static void handle_ws_connection(tcp::socket socket) {
    beast::flat_buffer buffer;
    http::request<http::string_body> request;
    try {
        http::read(socket, buffer, request);
        if (!websocket::is_upgrade(request)) {
            auto response = make_response(request, 400, json_error("websocket upgrade required"));
            http::write(socket, response);
            return;
        }
        run_ws_session(std::move(socket), std::move(request));
    } catch (const std::exception& error) {
        if (g_logger != nullptr) {
            g_logger->warn(std::string("WebSocket connection error: ") + error.what());
        }
    }
}

static void run_http_server(unsigned short port) {
    net::io_context context{1};
    tcp::acceptor acceptor(context, tcp::endpoint{net::ip::make_address("0.0.0.0"), port});
    acceptor.set_option(net::socket_base::reuse_address(true));

    for (;;) {
        tcp::socket socket(context);
        acceptor.accept(socket);
        std::thread(handle_http_connection, std::move(socket)).detach();
    }
}

static void run_ws_server(unsigned short port) {
    net::io_context context{1};
    tcp::acceptor acceptor(context, tcp::endpoint{net::ip::make_address("0.0.0.0"), port});
    acceptor.set_option(net::socket_base::reuse_address(true));

    for (;;) {
        tcp::socket socket(context);
        acceptor.accept(socket);
        std::thread(handle_ws_connection, std::move(socket)).detach();
    }
}

static void run_udp_listener(int slot, int recv_port) {
    std::string error;
    const SocketHandle socket_handle = open_udp_listener(recv_port, 500, &error);
    if (socket_handle == kInvalidSocket) {
        if (g_logger != nullptr) {
            g_logger->error("UDP listener startup failed for slot " + std::to_string(slot) + ": " + error);
        }
        return;
    }

    if (g_logger != nullptr) {
        g_logger->info("UDP telemetry listener started: slot " + std::to_string(slot) + " -> port " + std::to_string(recv_port));
    }

    std::string payload;
    while (g_backend != nullptr && g_backend->running()) {
        if (receive_udp_packet(socket_handle, payload)) {
            g_backend->handle_udp_telemetry(slot, payload);
        }
    }

    close_udp_listener(socket_handle);
}

static std::string default_config_path() {
    const std::vector<std::string> candidates = {
        "./config.yaml",
        "../config.yaml",
        "./config.json",
    };

    for (const auto& candidate : candidates) {
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
    }

    return "./config.yaml";
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    WSADATA data;
    WSAStartup(MAKEWORD(2, 2), &data);
#endif

    try {
        const std::string config_path = argc > 1 ? argv[1] : default_config_path();
        Config config = load_config(config_path);
        Logger logger(config.log_level, config.log_file);
        WsManager ws_manager;
        BackendService backend(config, &ws_manager, &logger);

        g_backend = &backend;
        g_ws_manager = &ws_manager;
        g_logger = &logger;

        backend.start_background_threads();

        std::vector<std::thread> udp_threads;
        for (const auto& item : config.port_map) {
            udp_threads.emplace_back(run_udp_listener, item.first, item.second.recv_port);
            udp_threads.back().detach();
        }

        logger.info("backend starting with config: " + config_path);
        logger.info("HTTP server listening on 0.0.0.0:" + std::to_string(config.http_port));
        logger.info("WebSocket server listening on 0.0.0.0:" + std::to_string(config.ws_port));

        std::thread http_thread(run_http_server, static_cast<unsigned short>(config.http_port));
        std::thread ws_thread(run_ws_server, static_cast<unsigned short>(config.ws_port));

        http_thread.join();
        ws_thread.join();
    } catch (const std::exception& error) {
        std::cerr << "Fatal error: " << error.what() << std::endl;
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
