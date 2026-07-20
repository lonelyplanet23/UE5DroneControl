# WBP_DroneInfoPanel 接入清单

编译本次代码后，将 `WBP_DroneInfoPanel` 的父类改为 `UDroneInfoPanelWidget`。该 C++ 基类取代了旧的 `UpdateFromSnapshot` 蓝图反射事件；旧事件及其图表可以删除。

把以下控件勾选 **Is Variable** 并使用完全相同的名字（全部为可选绑定，迁移可分步完成）：

| 区域 | 控件名 | 控件类型 |
| --- | --- | --- |
| 顶部 | `DroneNameText`, `DroneIdText`, `OnlineStatusText`, `TaskStatusText` | TextBlock |
| 顶部 | `CloseButton` | Button |
| 视频 | `VideoBrowser` | WebBrowser |
| 视频覆盖层 | `VideoStatusOverlay`, `VideoStatusText`, `RetryVideoButton` | Widget, TextBlock, Button |
| 遥测 | `BatteryText`, `SpeedText`, `LocationText`, `AltitudeText`, `AttitudeText`, `ArmedText`, `OffboardText`, `LastTelemetryText` | TextBlock |

布局采用固定宽度面板；视频容器使用 `SizeBox`，宽高比固定为 16:9。底部以两列 `UniformGridPanel` 或两列固定宽度 `VerticalBox` 放置数值，数值 TextBlock 设固定最小宽度/右对齐，避免位数变化时面板跳动。`VideoStatusOverlay` 放在视频区域的 Overlay 最上层。

关闭按钮、重试按钮和 WebBrowser 的 `LoadURL` 都已由 C++ 绑定：不要在 WBP 中再创建刷新 Timer 或自行加载视频。中键打开、切换无人机和关卡退出也都由 PlayerController 负责停止 0.2 秒遥测刷新并释放播放器。
