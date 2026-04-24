cmake_minimum_required(VERSION 3.16)
project(DroneBackend VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================
# Find packages (installed via vcpkg)
# ============================================================
find_package(httplib CONFIG REQUIRED)
find_package(Boost REQUIRED COMPONENTS system)
find_package(yaml-cpp REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)

# ============================================================
# Main executable
# ============================================================
set(SOURCES
    main.cpp
    core/config_loader.cpp
    conversion/coordinate_converter.cpp
    conversion/quaternion_utils.cpp
    conversion/gps_anchor_manager.cpp
    communication/udp_receiver.cpp
    communication/udp_sender.cpp
    drone/command_queue.cpp
    drone/state_machine.cpp
    drone/heartbeat_manager.cpp
    drone/drone_manager.cpp
)

set(HEADERS
    core/types.h
    core/logger.h
    core/config_loader.h
    conversion/coordinate_converter.h
    conversion/quaternion_utils.h
    conversion/gps_anchor_manager.h
    communication/udp_receiver.h
    communication/udp_sender.h
    drone/command_queue.h
    drone/state_machine.h
    drone/heartbeat_manager.h
    drone/drone_context.h
    drone/drone_manager.h
)

add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(${PROJECT_NAME} PRIVATE
    httplib::httplib
    Boost::system
    yaml-cpp
    nlohmann_json::nlohmann_json
    spdlog::spdlog
)

# ============================================================
# Unit tests
# ============================================================
set(TEST_SOURCES
    tests/test_main.cpp
    tests/test_coordinate_converter.cpp
    tests/test_quaternion_utils.cpp
    tests/test_gps_anchor_manager.cpp
    tests/test_command_queue.cpp
    tests/test_state_machine.cpp
)

add_executable(${PROJECT_NAME}_tests ${TEST_SOURCES} ${HEADERS})

target_include_directories(${PROJECT_NAME}_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(${PROJECT_NAME}_tests PRIVATE
    yaml-cpp
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    GTest::gtest_main
    GTest::gmock
)

# ============================================================
# Copy config.yaml to build directory
# ============================================================
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/config.yaml
    ${CMAKE_CURRENT_BINARY_DIR}/config.yaml COPYONLY)
