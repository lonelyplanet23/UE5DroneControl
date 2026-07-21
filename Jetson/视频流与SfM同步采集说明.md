# Jetson 视频流与 SfM 同步采集说明

## 1. 当前保存方案

`jetson_video_stream.py` 可以和现有 `jetson_bridge.py` 同时运行。默认情况下，Jetson 不长期保存视频或逐帧元数据：

```text
Jetson
├─ /image_raw -> H.264/RTSP -> MediaMTX 电脑 -> D:\DroneData\recordings
└─ GPS/位姿逐帧匹配 -> HTTP 批量上传 -> 后端电脑 -> D:\DroneData\metadata

UE -> 后端 GET /api/drones -> 取得 video_url -> MediaMTX WebRTC 页面
```

视频数据不会进入遥测 WebSocket。UE 只播放和展示，不负责录像。

当前测试环境中，MediaMTX、后端和 UE 都可以运行在 `192.168.10.30`。程序也支持以后分开部署：

| 角色 | 当前地址 | 将来能否独立电脑 |
|---|---|---|
| Jetson Orin NX | `192.168.10.1` | 一架无人机一台 Jetson |
| MediaMTX | `192.168.10.30` | 可以，视频保存在该电脑本地磁盘 |
| C++ 后端 | `192.168.10.30:8080` | 可以，元数据保存在该电脑本地磁盘 |
| UE | `192.168.10.30` | 可以，只需能访问后端和 MediaMTX |

以后如果三者地址不同，只需分别填写 `--mediamtx-host` 和 `--backend-base-url`，不需要改代码。

## 2. 数据内容

脚本只订阅：

- `/image_raw`
- `/camera_info`
- `/fmu/out/vehicle_global_position`
- `/fmu/out/vehicle_odometry`

它不发送飞控命令，不打开 PX4 串口或控制 UDP 端口，也不会再次打开 `/dev/video0`。

每个收到的图像帧都会生成一条元数据，包括：

- 图像时间、接收序号和估算丢帧数。
- 视频重连代次 `stream_generation` 和编码时间 `stream_pts_ns`。
- GPS 经纬度、海拔、精度和有效状态。
- NED 局部位置、速度和四元数。
- GPS/里程计匹配偏差。

GPS 通常只有几 Hz，脚本会在相邻有效样本之间按时间插值。逐帧对应不代表 GPS 传感器本身变成 30 Hz。

## 3. Jetson 安装依赖

```bash
sudo apt update
sudo apt install -y \
  python3-gi python3-gst-1.0 gir1.2-gstreamer-1.0 \
  gstreamer1.0-tools gstreamer1.0-rtsp \
  gstreamer1.0-plugins-base gstreamer1.0-plugins-good \
  gstreamer1.0-plugins-bad
```

检查插件：

```bash
gst-inspect-1.0 rtspclientsink
gst-inspect-1.0 nvv4l2h264enc
gst-inspect-1.0 splitmuxsink
gst-inspect-1.0 matroskamux
```

默认方案只需要前两个关键插件。后两个用于启用 Jetson 本地 MKV 备用录制。

FFmpeg 不是实时推流必需项，只在检查文件时可选安装：

```bash
sudo apt install -y ffmpeg
```

## 4. MediaMTX 电脑配置

将 [`Docs/MediaMTX/mediamtx.yml`](../Docs/MediaMTX/mediamtx.yml) 放到 MediaMTX 可执行文件旁并启动。

配置默认将视频保存到 MediaMTX 所在 Windows 电脑：

```text
D:\DroneData\recordings\drone-1\
```

录制格式为 fMP4，每 10 分钟一个视频段，不自动删除。MediaMTX 会自动创建子目录。

MediaMTX 电脑防火墙需要允许：

- TCP `8554`：Jetson 发布 RTSP。
- TCP `8889`：UE 打开 WebRTC 播放页。
- UDP `8189`：WebRTC 媒体传输。

如果 MediaMTX 以后换到另一台电脑：

1. 将配置中的 `webrtcAdditionalHosts` 改成那台电脑可被 UE 访问的局域网 IP。
2. Jetson 启动参数中的 `--mediamtx-host` 改成同一个 IP。
3. 后端保存的 `video_url` 会由 Jetson 脚本自动更新。

## 5. 后端电脑配置

后端新增接口：

```text
POST /api/video-metadata/batch
```

该接口不经过 WebSocket。默认保存目录在 [`Backend/config.yaml`](../Backend/config.yaml) 中：

```yaml
storage:
  path: "./data/drones.json"
  video_metadata_path: "D:/DroneData/metadata"
  video_metadata_max_batch: 300
```

后端会保存为：

```text
D:\DroneData\metadata\drone-1\mission_...\
├── session.json
├── completed.json
└── batches\
    ├── 000000000000.jsonl
    ├── 000000000001.jsonl
    └── ...
```

每批文件是不可变 JSONL。Jetson 在 HTTP 响应丢失后会用同一批次编号重试，后端发现文件已存在就返回成功，不会重复写入帧。

如果后端换到另一台电脑：

- 在那台电脑的 `config.yaml` 中设置它自己的保存磁盘。
- 允许 Jetson 访问 TCP `8080`。
- Jetson 使用 `--backend-base-url http://后端IP:8080`。
- UE 也将后端基础地址改成该电脑；MediaMTX 地址不必相同。

## 6. 复制和启动脚本

从 Windows 项目根目录复制：

```powershell
scp .\Jetson\jetson_video_stream.py jetson1@192.168.10.1:/home/jetson1/
```

Jetson 新终端加载 ROS2：

```bash
source /opt/ros/humble/setup.bash
source /home/jetson1/swarm_ws/install/setup.bash
```

确认话题：

```bash
ros2 topic hz /image_raw
ros2 topic hz /fmu/out/vehicle_global_position
ros2 topic hz /fmu/out/vehicle_odometry
```

当前同机部署的启动命令：

```bash
python3 /home/jetson1/jetson_video_stream.py \
  --drone-id 1 \
  --jetson-ip 192.168.10.1 \
  --mediamtx-host 192.168.10.30 \
  --backend-base-url http://192.168.10.30:8080 \
  --bitrate 8000000
```

未来 MediaMTX 和后端分开时，例如：

```bash
python3 /home/jetson1/jetson_video_stream.py \
  --drone-id 1 \
  --mediamtx-host 192.168.10.40 \
  --backend-base-url http://192.168.10.50:8080 \
  --bitrate 16000000 \
  --fps 30
```

这里视频保存在 `192.168.10.40`，元数据保存在 `192.168.10.50`，UE 可以运行在第三台电脑。

未来 1080p/30fps 做 SfM 时建议从 16 Mbps 开始实测。脚本会读取 `/image_raw` 的实际宽高，不会强行把低分辨率图像放大成 1080p。

如果 PX4 话题带命名空间，例如 `/px4_1/fmu/out/...`，增加：

```bash
--topic-prefix /px4_1
```

这个参数只改变 PX4 话题，不改变 `/image_raw` 和 `/camera_info`。

## 7. Jetson 空间和断网行为

默认参数为：

```text
--no-record-local
--no-local-metadata
--upload-metadata
--metadata-batch-size 90
--metadata-queue-size 3600
```

因此正常运行时不会在 Jetson 创建任务目录，也不会长期写入 MKV 或 CSV。

元数据上传使用有上限的内存队列。30fps、3600 帧约等于 2 分钟缓存：

- 后端短暂中断：保留批次并自动重试。
- 后端长时间中断且队列装满：新元数据会被丢弃，并增加 `metadata_dropped`。
- 不会因为后端离线无限占满 Jetson 内存或硬盘。
- 视频 RTSP 推流与元数据 HTTP 上传互相独立。

如果现场确实需要 Jetson 备用副本，可显式增加：

```bash
--record-local --local-metadata --record-dir /mnt/ssd/sfm_captures
```

只有使用这些参数时，Jetson 才会创建 `mission_...` 目录并保存 MKV、CSV、相机参数和任务信息。

## 8. 室内 GPS 行为

室内没有定位时：

- 视频、服务端录像和 UE 播放仍正常。
- 后端仍为每个收到的图像帧保存一条元数据。
- `lat_lon_valid` 为 `false`，经纬度为空，不写入伪造的 `0,0`。
- `/fmu/out/vehicle_odometry` 有效时，局部位置和姿态仍会保存。

PX4 时间一般是飞控启动后的微秒，摄像头 Header 常是 ROS/系统时间。脚本使用统一 ROS 时钟完成匹配，同时保留 PX4 原始时间。正式高精度 SfM 仍建议以后增加硬件触发、PPS/PTP 或相机精确时间戳。

## 9. 验证步骤

1. 启动后端和 MediaMTX。
2. 启动 Jetson 脚本，日志应显示 RTSP、WebRTC 和 metadata endpoint 三个地址。
3. 浏览器打开 `http://192.168.10.30:8889/drone-1`。
4. UE 中键打开 Drone 1 面板。
5. 检查 MediaMTX 电脑：

```text
D:\DroneData\recordings\drone-1
```

6. 检查后端电脑：

```text
D:\DroneData\metadata\drone-1
```

7. 断开后端网络几秒再恢复，确认 `metadata_queued` 先增加、随后下降，批次文件继续生成。
8. 按 `Ctrl+C`，脚本会尝试上传最后一批和完成标记；后端任务目录应出现 `completed.json`。

## 10. 重要限制

- MediaMTX 保存的是 fMP4，不再是上一版 Jetson 本地生成的 MKV。
- 视频文件与元数据位于不同目录，后续 SfM 整理程序应使用 `mission_id`、`stream_generation`、`stream_pts_ns` 和图像时间进行关联。
- 默认缓存只覆盖短暂断网，不承诺后端长时间离线时零丢失。若需要断网数小时仍零丢失，应增加 Jetson 小容量磁盘 spool 或任务结束后的补传机制。
- 录像磁盘容量仍需在 MediaMTX 电脑上管理。1080p、16 Mbps 约为每架无人机每小时 7.2 GB。
