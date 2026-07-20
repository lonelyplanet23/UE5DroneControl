# MediaMTX Demo 测试流

本工程中每架无人机的 `video_url` 由后端的 `GET /api/drones` 返回，UE 只在打开该机信息面板时读取它；它不会写入遥测 WebSocket。

将 `mediamtx.yml` 放到 MediaMTX 可执行文件旁并启动。对 Drone 1，后端注册或更新时填写：

```json
{ "video_url": "http://127.0.0.1:8889/drone-1" }
```

这里使用的是 MediaMTX 内置的浏览器 WebRTC 播放页。原生 WHEP 地址 `http://127.0.0.1:8889/drone-1/whep` 留给未来原生播放器，不应直接填进当前 UE WebBrowser。

没有摄像头时，可安装 FFmpeg 后循环推送本地 MP4：

```powershell
ffmpeg -stream_loop -1 -re -i .\demo.mp4 -an -c:v libx264 -pix_fmt yuv420p -preset veryfast -tune zerolatency -g 30 -r 30 -s 1280x720 -f rtsp -rtsp_transport tcp rtsp://127.0.0.1:8554/drone-1
```

验收时先在浏览器打开 `http://127.0.0.1:8889/drone-1`，再在 UE 中中键点击 Drone 1。打开/切换/关闭面板会分别加载新 URL、释放旧 URL 和将浏览器导航到 `about:blank`。如 WebRTC 页面不能建立连接，检查 MediaMTX 主机的 `webrtcAdditionalHosts` 是否填写了可由 UE 客户端访问的 LAN IP。
