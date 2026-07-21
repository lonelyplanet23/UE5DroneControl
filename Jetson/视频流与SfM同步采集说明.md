# Jetson 视频流与 SfM 同步采集说明

## 1. 程序作用

`jetson_video_stream.py` 是一个独立 ROS2 节点，可以和现有的 `jetson_bridge.py` 同时运行。

它只订阅以下话题，不发送飞控命令，也不会打开 PX4 串口、UDP 端口或摄像头设备：

- `/image_raw`
- `/camera_info`
- `/fmu/out/vehicle_global_position`
- `/fmu/out/vehicle_odometry`

程序同时完成两件事：

1. 将 `/image_raw` 编码为无音频 H.264，通过 RTSP 推送到 Windows 电脑上的 MediaMTX。
2. 在 Jetson 本地保存分段 MKV 视频、相机标定和 `frames.csv`，为每一个收到的图像帧记录对应 GPS 与局部位姿。

当前网络地址：

| 设备 | 地址 | 用途 |
|---|---|---|
| Jetson Orin NX | `192.168.10.1` | 订阅摄像头和 PX4 话题、编码与推流 |
| UE/Windows | `192.168.10.30` | 运行 MediaMTX、后端和 UE |

```text
摄像头驱动 -> /image_raw -> jetson_video_stream.py -> H.264/RTSP -> MediaMTX
                                               |                    |
PX4 -> global_position / odometry -------------+                    +-> WebRTC 页面 -> UE
                                               |
                                               +-> 本地 MKV + frames.csv
```

## 2. Jetson 安装依赖

```bash
sudo apt update
sudo apt install -y \
  python3-gi python3-gst-1.0 gir1.2-gstreamer-1.0 \
  gstreamer1.0-tools gstreamer1.0-rtsp \
  gstreamer1.0-plugins-base gstreamer1.0-plugins-good \
  gstreamer1.0-plugins-bad
```

检查关键插件：

```bash
gst-inspect-1.0 rtspclientsink
gst-inspect-1.0 nvv4l2h264enc
gst-inspect-1.0 splitmuxsink
gst-inspect-1.0 matroskamux
```

四条命令都应显示插件信息。`nvv4l2h264enc` 来自 Jetson 的 NVIDIA 多媒体组件；如果找不到，应先检查 JetPack/GStreamer NVIDIA 插件是否完整安装。

FFmpeg 不是实时推流必需项，仅用于检查录制文件时可选安装：

```bash
sudo apt install -y ffmpeg
```

## 3. Windows 启动 MediaMTX

将 [`Docs/MediaMTX/mediamtx.yml`](../Docs/MediaMTX/mediamtx.yml) 放到 MediaMTX 可执行文件旁，然后启动 MediaMTX。

Windows 防火墙需要允许：

- TCP `8554`：Jetson 发布 RTSP。
- TCP `8889`：UE WebBrowser 打开 MediaMTX WebRTC 页面。
- UDP `8189`：WebRTC 媒体传输。

当前配置已经写入 `webrtcAdditionalHosts: [192.168.10.30]`。

## 4. 将脚本复制到 Jetson

在 Windows 项目根目录执行，用户名按 Jetson 实际用户名修改：

```powershell
scp .\Jetson\jetson_video_stream.py jetson1@192.168.10.1:/home/jetson1/
```

也可以用 U 盘复制。Jetson 上只需要这一个 Python 文件。

## 5. 启动顺序

### 5.1 保持已有程序运行

保持原来的摄像头 ROS2 节点、Micro XRCE Agent 和 `jetson_bridge.py` 正常运行。不要再启动第二个直接读取 `/dev/video0` 的程序；视频脚本直接订阅已经存在的 `/image_raw`。

### 5.2 加载 ROS2 环境

打开另一个 Jetson 终端：

```bash
source /opt/ros/humble/setup.bash
source /home/jetson1/swarm_ws/install/setup.bash
```

先确认话题仍在发布：

```bash
ros2 topic hz /image_raw
ros2 topic hz /fmu/out/vehicle_global_position
ros2 topic hz /fmu/out/vehicle_odometry
```

### 5.3 启动推流和 SfM 采集

当前摄像头先使用默认 8 Mbps：

```bash
python3 /home/jetson1/jetson_video_stream.py \
  --drone-id 1 \
  --jetson-ip 192.168.10.1 \
  --mediamtx-host 192.168.10.30 \
  --record-dir /home/jetson1/sfm_captures \
  --bitrate 8000000
```

未来换成 1080p 摄像头做 SfM 时，脚本会自动读取 `/image_raw` 的实际宽高。建议先把码率提高到 16 Mbps：

```bash
python3 /home/jetson1/jetson_video_stream.py \
  --drone-id 1 \
  --jetson-ip 192.168.10.1 \
  --mediamtx-host 192.168.10.30 \
  --record-dir /home/jetson1/sfm_captures \
  --bitrate 16000000 \
  --fps 30
```

如果 PX4 话题带命名空间，例如 `/px4_1/fmu/out/...`，增加：

```bash
--topic-prefix /px4_1
```

这个参数只会作用于 PX4 的 GPS 和里程计话题，不会改变 `/image_raw` 和 `/camera_info`。

## 6. 自动生成的地址

Drone 1 默认使用：

- Jetson 推流地址：`rtsp://192.168.10.30:8554/drone-1`
- UE 播放页面：`http://192.168.10.30:8889/drone-1`
- 后端接口：`PUT http://192.168.10.30:8080/api/drones/1`

脚本会自动向现有后端写入：

```json
{
  "video_url": "http://192.168.10.30:8889/drone-1"
}
```

如果后端暂时没启动，脚本会继续推流和采集，并每 5 秒重试。若只想测试视频、不更新后端，增加 `--no-update-backend`。如果后端中的无人机 ID 不同，可用 `--backend-drone-id` 单独指定。

## 7. SfM 文件说明

每次启动都会创建一个新目录，例如：

```text
/home/jetson1/sfm_captures/mission_20260721_153000_drone_1/
├── mission.json
├── camera_info.json
├── frames.csv
├── video_g001_00000.mkv
├── video_g001_00001.mkv
└── ...
```

- `mission.json`：本次任务、地址、帧数和丢帧统计。
- `camera_info.json`：相机内参和畸变参数。正式 SfM 前必须完成相机标定；若 `K[0] == 0`，程序会提示未标定。
- `video_gNNN_XXXXX.mkv`：无音频 H.264 视频，默认每 10 分钟分段。网络断开并重连后 `gNNN` 会递增。
- `frames.csv`：每个收到的 `/image_raw` 回调对应一行。

关键 CSV 字段：

- `frame_index`：ROS 图像接收序号。
- `image_timestamp_ns`：原始图像 Header 时间；Header 无效时使用 ROS 接收时间。
- `sync_timestamp_us` / `sync_basis`：实际用于匹配 GPS/里程计的统一 ROS 时钟及其来源。
- `stream_accepted`、`stream_generation`、`stream_pts_ns`：该帧是否交给编码器、属于哪个视频代次、在该视频代次中的时间位置。
- `latitude`、`longitude`、`altitude_amsl_m`：与该帧匹配或插值后的全球位置。
- `gps_px4_timestamp_sample_us`：匹配位置对应的原始 PX4 时间，不能直接当作 Unix 时间。
- `local_north_m`、`local_east_m`、`local_down_m` 和四元数：最近的本地里程计位姿。
- `estimated_source_drops`、`stream_error`：排查源丢帧或推流失败。

PX4 时间通常是飞控启动后的微秒，而摄像头 Header 常是 ROS/系统时间。脚本不会直接比较这两种时间；它使用同一个 ROS 时钟记录消息到达时间并匹配，同时把 PX4 原始时间保留在 CSV 中。

室内没有 GPS 定位时：

- 面板和视频仍可正常工作。
- `frames.csv` 仍然每帧写一行。
- `lat_lon_valid` 为 `False`，经纬度字段为空，不会伪造 `0,0` 坐标。
- 局部位姿仍可由 `/fmu/out/vehicle_odometry` 填写。

## 8. 验证方法

1. 在 UE/Windows 浏览器打开 `http://192.168.10.30:8889/drone-1`，确认画面出现。
2. 调用后端 `GET /api/drones`，确认 Drone 1 的 `video_url` 是上述页面地址。
3. 在 UE 中中键点击 Drone 1，确认信息面板播放同一画面。
4. 在 Jetson 上查看输出目录，确认 MKV 文件大小持续增长且 `frames.csv` 持续增加行。
5. 如果安装了 FFmpeg，可检查录制参数：

```bash
ffprobe /home/jetson1/sfm_captures/mission_*/video_g001_00000.mkv
```

6. 按 `Ctrl+C` 停止。日志应出现 `Video pipeline, recorder and metadata writer stopped`，MKV 文件完成封装，`mission.json` 中 `completed` 变为 `true`。

## 9. 常见问题

### 浏览器打不开播放页

先从 Jetson 检查 Windows 端口：

```bash
nc -vz 192.168.10.30 8554
nc -vz 192.168.10.30 8889
```

再检查 MediaMTX 是否运行、Windows 防火墙是否放行、`webrtcAdditionalHosts` 是否是 `192.168.10.30`。

### 提示找不到 `rtspclientsink`

安装 `gstreamer1.0-rtsp`，然后重新执行 `gst-inspect-1.0 rtspclientsink`。

### 提示找不到 `nvv4l2h264enc`

这是 Jetson 硬件编码插件缺失，不要临时改成 CPU 编码长期运行。检查 JetPack、`nvidia-l4t-gstreamer` 和 NVIDIA 多媒体组件。

### 有 GPS 话题但 CSV 没有经纬度

```bash
ros2 topic echo /fmu/out/vehicle_global_position --once
```

重点看 `lat_lon_valid`、`alt_valid`、`eph` 和 `epv`。室内持续发布消息但没有有效定位属于正常情况。

### `stream_queue_drops` 持续增加

说明原始图像到达速度超过编码或网络处理速度。先确认硬件编码插件被使用，再检查局域网稳定性、磁盘写入速度和实际帧率；不建议简单增大队列，因为会明显增加延迟。

## 10. 当前精度边界

这版保证“每个收到的图像帧都有一行元数据”，但 GPS 通常只有几 Hz，无法产生真正每帧独立测量的 GPS。脚本会在相邻有效 GPS 样本之间按时间插值；CSV 是逐帧对应，不代表 GPS 传感器本身变成 30 Hz。

用于正式高精度 SfM 前，还需要实测确认摄像头标定、曝光/快门、ROS 时间同步、GPS 精度、视频丢帧率和磁盘吞吐。若以后能使用硬件触发、PPS/PTP 或相机自带精确时间戳，应优先替换现在基于 ROS 到达时间的同步方式。
