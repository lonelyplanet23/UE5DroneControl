#pragma once

#include "command_queue.h"
#include "core/types.h"
#include "state_machine.h"

#include <memory>
#include <string>

struct DroneContext {
    int drone_id = 0;
    int slot = 0;
    std::string name;

    std::unique_ptr<StateMachine> state_machine;
    std::unique_ptr<CommandQueue> command_queue;

    TelemetryData latest_telemetry{};
    bool has_telemetry = false;
    double last_telemetry_unix = 0.0;

    bool low_battery_alert_active = false;

    double last_ned_x = 0.0;
    double last_ned_y = 0.0;
    double last_ned_z = -1.0;

    std::string jetson_ip = "192.168.30.104";
    int send_port = 8889;

    DroneContext(int id, int slot_id, const std::string& drone_name)
        : drone_id(id)
        , slot(slot_id)
        , name(drone_name)
        , state_machine(std::make_unique<StateMachine>())
        , command_queue(std::make_unique<CommandQueue>())
    {
    }
};
