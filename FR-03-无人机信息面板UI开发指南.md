# FR-03 无人机信息面板UI - 蓝图创建指南

本文档描述在C++改造完成后，如何在UE5编辑器中创建`WBP_DroneInfoPanel`并集成到HUD中。

## 前置条件

- C++代码已经编译通过
- 项目已打开在UE5编辑器中
- `Content/DroneOps/UI/` 文件夹已创建

---

## 步骤 1：创建 WBP_DroneInfoPanel Widget Blueprint

### 1.1 新建Widget
1. 在 **Content Browser** → `Content/DroneOps/UI/`
2. 右键 → **User Interface → Widget Blueprint**
3. 命名为 `WBP_DroneInfoPanel`

### 1.2 UI 布局设计

打开`WBP_DroneInfoPanel`，在 **Widget Tree** 中按以下结构搭建：

```
WBP_DroneInfoPanel
└── Size Box (宽度固定 320, 高度 Fill)
    └── Border (Brush: 浅色背景，圆角，边框)
        └── Vertical Box (Padding: 10)
            ├── Text (Title)
            │   └── Content: "无人机信息"
            │   └── Font: 加粗, Size 18
            ├── Spacer (Height 8)
            ├── ─── Separator ───
            ├── Spacer (Height 8)
            ├── Horizontal Box (ID行)
            │   ├── Text: "ID: " (Size 12, 宽度 80)
            │   └── Text [DroneIdText] (绑定)
            ├── Horizontal Box (名称行)
            │   ├── Text: "名称: " (Size 12, 宽度 80)
            │   └── Text [DroneNameText] (绑定)
            ├── Horizontal Box (状态行)
            │   ├── Text: "状态: " (Size 12, 宽度 80)
            │   └── Text [StatusText] (绑定，颜色可变)
            ├── Spacer (Height 4)
            ├── Horizontal Box (X坐标)
            │   ├── Text: "位置 X: " (Size 12, 宽度 80)
            │   └── Text [PosXText] (绑定)
            ├── Horizontal Box (Y坐标)
            │   ├── Text: "位置 Y: " (Size 12, 宽度 80)
            │   └── Text [PosYText] (绑定)
            ├── Horizontal Box (高度)
            │   ├── Text: "高度: " (Size 12, 宽度 80)
            │   └── Text [AltitudeText] (绑定)
            ├── Horizontal Box (速度)
            │   ├── Text: "速度: " (Size 12, 宽度 80)
            │   └── Text [SpeedText] (绑定)
            ├── Spacer (Height 4)
            ├── Horizontal Box (偏航)
            │   ├── Text: "Yaw: " (Size 12, 宽度 80)
            │   └── Text [YawText] (绑定)
            ├── Horizontal Box (俯仰)
            │   ├── Text: "Pitch: " (Size 12, 宽度 80)
            │   └── Text [PitchText] (绑定)
            ├── Horizontal Box (滚转)
            │   ├── Text: "Roll: " (Size 12, 宽度 80)
            │   └── Text [RollText] (绑定)
            ├── Spacer (Height 4)
            ├── Horizontal Box (最后更新)
            │   ├── Text: "更新: " (Size 12, 宽度 80)
            │   └── Text [LastUpdateText] (绑定)
            ├── Spacer (Height 10)
            └── Button (关闭按钮)
                └── Text: "关闭"
```

### 1.3 创建绑定变量

在 **Graph** → **Variables** 添加这些变量：

| 变量名 | 类型 | 可读写 | 说明 |
|--------|------|--------|------|
| `CurrentDroneId` | `Integer` | Blueprint Read/Write | 当前显示的无人机ID |
| `DroneIdText` | `Text` | Blueprint Read/Write | ID显示文本 |
| `DroneNameText` | `Text` | Blueprint Read/Write | 名称显示文本 |
| `StatusText` | `Text` | Blueprint Read/Write | 状态显示文本 |
| `StatusColor` | `Linear Color` | Blueprint Read/Write | 状态文本颜色 |
| `PosXText` | `Text` | Blueprint Read/Write | X坐标文本 |
| `PosYText` | `Text` | Blueprint Read/Write | Y坐标文本 |
| `AltitudeText` | `Text` | Blueprint Read/Write | 高度文本 |
| `SpeedText` | `Text` | Blueprint Read/Write | 速度文本 |
| `YawText` | `Text` | Blueprint Read/Write | 偏航文本 |
| `PitchText` | `Text` | Blueprint Read/Write | 俯仰文本 |
| `RollText` | `Text` | Blueprint Read/Write | 滚转文本 |
| `LastUpdateText` | `Text` | Blueprint Read/Write | 更新时间文本 |

### 1.4 绑定文本到UI

在 **Widget Tree** 选中每个Text控件，在 **Details** → **Content → Text** → 选择 **Bind** → 选择对应的绑定变量。

### 1.5 创建 `UpdateFromSnapshot` 函数

1. **Graph** → 右键 → **Function** → 命名 `UpdateFromSnapshot`
2. 添加两个输入参数：
   - `Snapshot`: `Drone Telemetry Snapshot` (struct)
   - `DroneName`: `String`

3. **函数蓝图逻辑**:

```blueprint
// 设置当前无人机ID
Set CurrentDroneId = Snapshot.DroneId

// ID文本
Set DroneIdText = ToText(CurrentDroneId)

// 名称文本
Set DroneNameText = ToText(DroneName)

// 状态文本和颜色
Branch (Snapshot.Availability == Online)
  → True: Set StatusText = "在线", Set StatusColor = Green
  → False: Branch (Snapshot.Availability == Offline)
           → True: Set StatusText = "离线", Set StatusColor = Gray
           → False: Set StatusText = "失联", Set StatusColor = Red

// 位置 (UE单位是厘米 → 转换为米显示)
float X = Snapshot.WorldLocation.X / 100.0
float Y = Snapshot.WorldLocation.Y / 100.0
Set PosXText = ToText(X, 2 decimal places) + " m"
Set PosYText = ToText(Y, 2 decimal places) + " m"

// 高度 (已经是米)
Set AltitudeText = ToText(Snapshot.Altitude, 2 decimal places) + " m"

// 速度 (计算速度向量模长)
float Speed = Snapshot.Velocity.Size()
Set SpeedText = ToText(Speed, 2 decimal places) + " m/s"

// 姿态角度
Set YawText = ToText(Snapshot.Attitude.Yaw, 1 decimal) + "°"
Set PitchText = ToText(Snapshot.Attitude.Pitch, 1 decimal) + "°"
Set RollText = ToText(Snapshot.Attitude.Roll, 1 decimal) + "°"

// 最后更新时间 (相对于游戏开始时间)
float Now = Get World → Get Time Seconds
float Delta = Now - Snapshot.LastUpdateTime
Branch (Delta < 60.0)
  → True: Set LastUpdateText = ToText(Delta, 1 decimal) + "s 前"
  → False: Set LastUpdateText = "> 1min 前"
```

### 1.6 绑定关闭按钮

1. 在 **Widget Tree** 选中关闭按钮
2. **Details** → **On Clicked** → **Add** → `WBP_DroneInfoPanel` → `Remove From Parent`

---

## 步骤 2：修改 WBP_DroneOpsHUD

打开 `WBP_DroneOpsHUD`:

### 2.1 添加变量

添加变量：
- 名称: `CurrentDroneInfoPanel`
- 类型: `WBP_DroneInfoPanel` (Object Reference)
- 默认: `None`

### 2.2 绑定打开事件

在 **Event Graph**:

```
Event Construct
  → Get Owning Player Controller
  → Cast To DroneOpsPlayerController
  → From Cast Output → On Open Drone Info Panel Requested
  → Bind the event (Create a custom event "HandleOpenDroneInfoPanel")
```

### 2.3 实现 `HandleOpenDroneInfoPanel`

输入参数: `DroneId` (Integer)

```blueprint
// 如果已经有面板打开，先关闭
If CurrentDroneInfoPanel Is Valid
  → CurrentDroneInfoPanel → Remove From Parent

// Get DroneRegistry from GameInstance
Get Game Instance → Get Subsystem → Drone Registry Subsystem

// Get telemetry snapshot
Call DroneRegistry → Get Telemetry (DroneId)
  → Out: Snapshot (DroneTelemetrySnapshot)

// Get drone descriptor
Call DroneRegistry → Get Drone Descriptor (DroneId)
  → Out: Descriptor (FDroneDescriptor)

// Create widget instance
Create Widget → Owner: this → Class: WBP_DroneInfoPanel
  → Output: NewPanel

// Call update function
NewPanel → UpdateFromSnapshot(Snapshot, Descriptor.Name)

// Add to viewport
NewPanel → Add To Viewport

// Save reference
Set CurrentDroneInfoPanel = NewPanel
```

---

## 步骤 3：测试运行

### 测试步骤

1. **确保你的关卡中**:
   - GameMode 是 `BP_DroneOpsGameMode`
   - 至少放置了一个 `ARealTimeDroneReceiver` Actor
   - 正确设置了 `DroneId`, `DroneName`, `ListenPort`

2. **点击 Play 运行**

3. **测试流程**:
   - [ ] 将鼠标**悬停**在无人机模型上
   - [ ] **按下鼠标滚轮中键**
   - [ ] ✅ 信息面板弹出，显示所有字段
   - [ ] ✅ 状态颜色正确（在线绿色、离线灰色、失联红色）
   - [ ] ✅ 单位正确（位置米、高度米、速度m/s）
   - [ ] 点击**关闭**按钮 → 面板消失
   - [ ] 鼠标**不悬停**无人机按中键 → 不弹出面板

### 验收标准 (对照需求文档)

| 需求 | 验收 |
|------|------|
| 只有悬停无人机+中键才弹出 | ✓ |
| 显示无人机ID/名称 | ✓ |
| 显示在线状态 | ✓ |
| 显示当前位置 | ✓ |
| 显示当前高度 | ✓ |
| 显示当前速度 | ✓ |
| 显示当前姿态(Yaw/Pitch/Roll) | ✓ |
| 显示最近更新时间 | ✓ |
| 数据更新后面板信息自动刷新 | 下一版本优化，当前需要重新打开刷新 |
| 扩展性：可后续新增字段 | ✓ (数据结构已预留) |

---

## 故障排除

### 问题1: 按中键没反应

- 检查 `DefaultInput.ini` 是否有 `ShowInfo` 绑定到 `MiddleMouseButton`
- 检查 `DroneOpsPlayerController.SetupInputComponent()` 是否绑定了 `OnShowInfo`
- 检查 **Output Log** 有没有 `[FR-03] Opening info panel for DroneId: X` 日志

### 问题2: 面板弹出但是所有字段都是空

- 检查 `ARealTimeDroneReceiver` 上的 `TelemetryComponent` 是否自动创建
- 检查 `DroneRegistry` 是否有这架无人机的注册
- 检查 `DroneId` 是否大于 0

### 问题3: 编译失败

- 确保 C++ 已经重新编译，关闭UE再重新打开项目
- 确保 `DroneOpsTypes.h` 中 `FDroneTelemetrySnapshot` 结构体已正确生成

---

## 扩展指南：新增字段

如果后续需要新增字段（比如电池电量），只需：

1. **C++层**: 在 `FDroneTelemetrySnapshot` 添加新字段
2. **UI层**: 在 `WBP_DroneInfoPanel` 中新增一行Horizontal Box，添加Text和绑定
3. **更新函数**: 在 `UpdateFromSnapshot` 中添加新字段的转换逻辑

不需要修改架构，符合需求文档的扩展性要求。
