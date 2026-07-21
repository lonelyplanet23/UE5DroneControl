#!/usr/bin/env bash

# Read-only diagnostic for camera/PX4 timestamp synchronization.
# Keep the camera node, PX4 uXRCE-DDS client and Micro XRCE-DDS Agent running.
# Usage:
#   bash jetson_sfm_sync_env_check.sh [PX4_WORKSPACE_INSTALL_SETUP]
# Example:
#   bash jetson_sfm_sync_env_check.sh /home/jetson1/px4_ros_ws/install/setup.bash

set +e

RUN_STAMP="$(date +%Y%m%d_%H%M%S)"
OUTPUT_FILE="jetson_sfm_sync_${RUN_STAMP}.txt"
PX4_SETUP_FILE="${1:-}"

exec > >(tee "$OUTPUT_FILE") 2>&1

section() {
    printf '\n============================================================\n'
    printf '%s\n' "$1"
    printf '============================================================\n'
}

run_timeout() {
    local duration="$1"
    shift
    timeout "$duration" "$@"
}

if ! command -v ros2 >/dev/null 2>&1 && [ -r /opt/ros/humble/setup.bash ]; then
    # shellcheck disable=SC1091
    source /opt/ros/humble/setup.bash
fi

if [ -n "$PX4_SETUP_FILE" ]; then
    if [ -r "$PX4_SETUP_FILE" ]; then
        # shellcheck disable=SC1090
        source "$PX4_SETUP_FILE"
    else
        printf 'WARNING: requested PX4 setup file is not readable: %s\n' "$PX4_SETUP_FILE"
    fi
fi

section "1. ROS AND PX4 MESSAGE SUPPORT"
printf 'Time:          %s\n' "$(date --iso-8601=seconds 2>/dev/null || date)"
printf 'ROS_DISTRO:    %s\n' "${ROS_DISTRO:-not set}"
printf 'ROS_DOMAIN_ID: %s\n' "${ROS_DOMAIN_ID:-0 (default)}"
printf 'PX4 setup:     %s\n' "${PX4_SETUP_FILE:-not supplied}"

if ! command -v ros2 >/dev/null 2>&1; then
    printf 'ERROR: ros2 was not found. Source ROS 2 Humble and run again.\n'
    exit 1
fi

printf 'px4_msgs package: '
ros2 pkg prefix px4_msgs 2>/dev/null || printf 'not available in this terminal\n'

printf '\nRelevant running processes:\n'
ps -ef 2>/dev/null | grep -Ei 'MicroXRCEAgent|micro_ros_agent|uxrce|v4l2_camera|jetson_bridge' | grep -v grep

section "2. REQUIRED TOPICS"
for topic_name in \
    /image_raw \
    /camera_info \
    /fmu/out/vehicle_global_position \
    /fmu/out/vehicle_odometry; do
    printf '\nTopic: %s\n' "$topic_name"
    printf 'Type:  '
    run_timeout 4s ros2 topic type "$topic_name" 2>&1
    run_timeout 5s ros2 topic info "$topic_name" --verbose 2>&1
done

section "3. MESSAGE DEFINITIONS"
for message_type in \
    sensor_msgs/msg/Image \
    sensor_msgs/msg/CameraInfo \
    px4_msgs/msg/VehicleGlobalPosition \
    px4_msgs/msg/VehicleOdometry; do
    printf '\nInterface: %s\n' "$message_type"
    ros2 interface show "$message_type" 2>&1 | head -n 100
done

section "4. CAMERA MESSAGE SAMPLE WITHOUT PIXEL DATA"
printf 'Host time before sample (ns): %s\n' "$(date +%s%N 2>/dev/null)"
for field_name in header width height encoding is_bigendian step; do
    printf '\n/image_raw.%s:\n' "$field_name"
    run_timeout 8s ros2 topic echo /image_raw --once --field "$field_name" 2>&1 | head -n 20
done
printf 'Host time after sample (ns):  %s\n' "$(date +%s%N 2>/dev/null)"

section "5. PX4 GLOBAL POSITION SAMPLE"
printf 'Host time before sample (ns): %s\n' "$(date +%s%N 2>/dev/null)"
run_timeout 8s ros2 topic echo /fmu/out/vehicle_global_position --once 2>&1 | head -n 100
printf 'Host time after sample (ns):  %s\n' "$(date +%s%N 2>/dev/null)"

section "6. PX4 ODOMETRY SAMPLE"
printf 'Host time before sample (ns): %s\n' "$(date +%s%N 2>/dev/null)"
run_timeout 8s ros2 topic echo /fmu/out/vehicle_odometry --once 2>&1 | head -n 120
printf 'Host time after sample (ns):  %s\n' "$(date +%s%N 2>/dev/null)"

section "7. TOPIC RATES"
for topic_name in \
    /image_raw \
    /camera_info \
    /fmu/out/vehicle_global_position \
    /fmu/out/vehicle_odometry; do
    printf '\nRate sample for %s (6 seconds):\n' "$topic_name"
    run_timeout 6s ros2 topic hz "$topic_name" 2>&1
done

section "8. RESULT"
printf 'Output file: %s\n' "$OUTPUT_FILE"
printf 'Send this file back for synchronization design and implementation.\n'
