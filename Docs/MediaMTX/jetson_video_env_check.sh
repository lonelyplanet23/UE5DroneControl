#!/usr/bin/env bash

# Jetson video/ROS environment diagnostic script.
# Read-only: it does not install packages or change system configuration.
#
# Usage:
#   bash jetson_video_env_check.sh [MEDIAMTX_IP] [DRONE_ID]
# Example:
#   bash jetson_video_env_check.sh 192.168.1.100 d1

set +e

MEDIAMTX_HOST="${1:-}"
DRONE_ID="${2:-d1}"
RUN_STAMP="$(date +%Y%m%d_%H%M%S)"
OUTPUT_FILE="jetson_video_env_${RUN_STAMP}.txt"

exec > >(tee "$OUTPUT_FILE") 2>&1

section() {
    printf '\n============================================================\n'
    printf '%s\n' "$1"
    printf '============================================================\n'
}

command_status() {
    local command_name="$1"
    if command -v "$command_name" >/dev/null 2>&1; then
        printf '[OK]      %-24s %s\n' "$command_name" "$(command -v "$command_name")"
    else
        printf '[MISSING] %-24s\n' "$command_name"
    fi
}

gst_plugin_status() {
    local plugin_name="$1"
    if command -v gst-inspect-1.0 >/dev/null 2>&1 && \
       gst-inspect-1.0 "$plugin_name" >/dev/null 2>&1; then
        printf '[OK]      %s\n' "$plugin_name"
    else
        printf '[MISSING] %s\n' "$plugin_name"
    fi
}

run_with_timeout() {
    local duration="$1"
    shift
    if command -v timeout >/dev/null 2>&1; then
        timeout "$duration" "$@"
    else
        "$@"
    fi
}

section "1. BASIC SYSTEM"
printf 'Time:            %s\n' "$(date --iso-8601=seconds 2>/dev/null || date)"
printf 'Hostname:        %s\n' "$(hostname 2>/dev/null)"
printf 'Architecture:    %s\n' "$(uname -m 2>/dev/null)"
printf 'Kernel:          %s\n' "$(uname -r 2>/dev/null)"
printf 'Ubuntu:          %s\n' "$(lsb_release -ds 2>/dev/null || grep PRETTY_NAME /etc/os-release 2>/dev/null)"
printf 'Jetson model:    '
tr -d '\0' </proc/device-tree/model 2>/dev/null || printf 'Unknown'
printf '\n'
printf 'Memory:          '
free -h 2>/dev/null | awk '/^Mem:/ {print $2 " total, " $7 " available"}'
printf 'Disk:            '
df -h / 2>/dev/null | awk 'NR==2 {print $2 " total, " $4 " available"}'

section "2. JETPACK / L4T"
if [ -r /etc/nv_tegra_release ]; then
    printf 'L4T release:     '
    head -n 1 /etc/nv_tegra_release
else
    printf 'L4T release:     /etc/nv_tegra_release not found\n'
fi
printf 'JetPack package: '
dpkg-query -W -f='${Version}\n' nvidia-jetpack 2>/dev/null || printf 'nvidia-jetpack meta-package not installed\n'
printf 'CUDA:            '
if command -v nvcc >/dev/null 2>&1; then
    nvcc --version 2>/dev/null | tail -n 1
elif [ -r /usr/local/cuda/version.json ]; then
    grep -o '"version"[^,]*' /usr/local/cuda/version.json 2>/dev/null | head -n 1
else
    printf 'not detected\n'
fi

section "3. PYTHON / OPENCV"
command_status python3
python3 --version 2>&1
if command -v python3 >/dev/null 2>&1; then
    python3 -c "import cv2; print('OpenCV Python:   ' + cv2.__version__)" 2>/dev/null || \
        printf 'OpenCV Python:   not importable\n'
    python3 -c "import gi; print('PyGObject/gi:    available')" 2>/dev/null || \
        printf 'PyGObject/gi:    not importable\n'
    python3 -c "import rclpy; print('rclpy:           available')" 2>/dev/null || \
        printf 'rclpy:           not importable before ROS setup\n'
fi

section "4. ROS 2"
if ! command -v ros2 >/dev/null 2>&1; then
    ROS_SETUP_FILE=""
    for candidate in /opt/ros/humble/setup.bash /opt/ros/*/setup.bash; do
        if [ -r "$candidate" ]; then
            ROS_SETUP_FILE="$candidate"
            break
        fi
    done
    if [ -n "$ROS_SETUP_FILE" ]; then
        # shellcheck disable=SC1090
        source "$ROS_SETUP_FILE"
        printf 'Sourced:         %s\n' "$ROS_SETUP_FILE"
    fi
fi

printf 'ROS_DISTRO:      %s\n' "${ROS_DISTRO:-not set}"
printf 'ROS_DOMAIN_ID:   %s\n' "${ROS_DOMAIN_ID:-0 (default)}"
command_status ros2

if command -v ros2 >/dev/null 2>&1; then
    printf '\nROS nodes:\n'
    run_with_timeout 5s ros2 node list 2>&1

    printf '\nROS topics and message types:\n'
    run_with_timeout 5s ros2 topic list -t 2>&1

    for topic_name in /image_raw /camera/image_raw /camera_info /camera/camera_info; do
        topic_type="$(run_with_timeout 4s ros2 topic type "$topic_name" 2>/dev/null)"
        if [ -n "$topic_type" ]; then
            printf '\nDetected camera topic: %s\n' "$topic_name"
            printf 'Message type:          %s\n' "$topic_type"
            run_with_timeout 5s ros2 topic info "$topic_name" --verbose 2>&1

            if [[ "$topic_type" == *"Image"* ]] && [[ "$topic_type" != *"CameraInfo"* ]]; then
                printf 'Image width:           '
                run_with_timeout 6s ros2 topic echo "$topic_name" --once --field width 2>/dev/null | head -n 1
                printf 'Image height:          '
                run_with_timeout 6s ros2 topic echo "$topic_name" --once --field height 2>/dev/null | head -n 1
                printf 'Image encoding:        '
                run_with_timeout 6s ros2 topic echo "$topic_name" --once --field encoding 2>/dev/null | head -n 1
                printf '\nApproximate topic rate (sample for 6 seconds):\n'
                run_with_timeout 6s ros2 topic hz "$topic_name" 2>&1
                printf '\nApproximate topic bandwidth (sample for 6 seconds):\n'
                run_with_timeout 6s ros2 topic bw "$topic_name" 2>&1
            fi
        fi
    done
else
    printf 'ROS 2 CLI was not found.\n'
fi

section "5. CAMERA DEVICES"
if compgen -G '/dev/video*' >/dev/null 2>&1; then
    printf '/dev/video devices:\n'
    ls -l /dev/video* 2>/dev/null
else
    printf 'No /dev/video* device detected. This can be normal for a ROS-only or CSI pipeline.\n'
fi

command_status v4l2-ctl
if command -v v4l2-ctl >/dev/null 2>&1; then
    printf '\nv4l2-ctl --list-devices:\n'
    v4l2-ctl --list-devices 2>&1
    for video_device in /dev/video*; do
        [ -e "$video_device" ] || continue
        printf '\nFormats for %s:\n' "$video_device"
        v4l2-ctl --device="$video_device" --list-formats-ext 2>&1 | head -n 80
    done
fi

section "6. GSTREAMER AND JETSON HARDWARE ENCODING"
command_status gst-launch-1.0
command_status gst-inspect-1.0
gst-launch-1.0 --version 2>&1 | head -n 2

printf '\nRequired/recommended GStreamer elements:\n'
for gst_element in \
    appsrc videoconvert videoscale capsfilter \
    nvvidconv nvvideoconvert nvv4l2h264enc omxh264enc \
    nvarguscamerasrc v4l2src filesrc decodebin \
    h264parse rtph264pay rtspclientsink; do
    gst_plugin_status "$gst_element"
done

if command -v gst-inspect-1.0 >/dev/null 2>&1 && \
   gst-inspect-1.0 nvv4l2h264enc >/dev/null 2>&1; then
    printf '\nnvv4l2h264enc selected properties:\n'
    gst-inspect-1.0 nvv4l2h264enc 2>/dev/null | \
        grep -E 'bitrate|iframeinterval|idrinterval|insert-sps-pps|profile|control-rate' | \
        head -n 40
fi

section "7. FFMPEG"
command_status ffmpeg
if command -v ffmpeg >/dev/null 2>&1; then
    ffmpeg -version 2>&1 | head -n 3
    printf '\nDetected H.264-related encoders:\n'
    ffmpeg -hide_banner -encoders 2>/dev/null | grep -Ei '264|nvenc|v4l2m2m|nvmpi' | head -n 40
fi

section "8. NETWORK"
printf 'IPv4 addresses:\n'
ip -4 -brief address show 2>/dev/null
printf '\nRoutes:\n'
ip route 2>/dev/null

if [ -n "$MEDIAMTX_HOST" ]; then
    printf '\nMediaMTX host supplied: %s\n' "$MEDIAMTX_HOST"
    printf 'DroneId supplied:       %s\n' "$DRONE_ID"
    printf 'RTSP publish URL:       rtsp://%s:8554/%s\n' "$MEDIAMTX_HOST" "$DRONE_ID"
    printf 'WebRTC page URL:        http://%s:8889/%s\n' "$MEDIAMTX_HOST" "$DRONE_ID"

    printf '\nPing test:\n'
    ping -c 2 -W 2 "$MEDIAMTX_HOST" 2>&1

    if command -v nc >/dev/null 2>&1; then
        printf '\nMediaMTX TCP port checks (server must already be running):\n'
        nc -zvw2 "$MEDIAMTX_HOST" 8554 2>&1
        nc -zvw2 "$MEDIAMTX_HOST" 8889 2>&1
    else
        printf '\nnetcat/nc is not installed; TCP port checks skipped.\n'
    fi
else
    printf 'No MediaMTX IP was supplied; connectivity and port checks were skipped.\n'
    printf 'Run again later with: bash %s <MediaMTX_IP> <DroneId>\n' "$0"
fi

section "9. SUMMARY"
printf 'Known target:     Jetson Orin NX, one Jetson per drone, no audio\n'
printf 'Expected stream:  H.264, 1280x720, 30 fps, RTSP -> MediaMTX -> WebRTC\n'
printf 'Output file:      %s\n' "$OUTPUT_FILE"
printf '\nPlease send this output file, or clear photos covering sections 1-9.\n'
printf 'Diagnostic complete.\n'

