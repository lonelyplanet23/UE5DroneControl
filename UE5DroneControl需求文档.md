1. 项目概述
本项目旨在利用 Unreal Engine 5 构建无人机数字孪生平台，实现物理实体与虚拟模型的实时双向映射。通过高精度场景还原、实时遥测数据可视化及指令下发，解决远程监控与任务仿真需求。

---
2. 快速跳转链接 (Quick Links)
开发指南：
- UE5 前端开发文档 前端同学由此跳转
- 后端开发文档 后端同学由此跳转
技术标准：
- 坐标系转换说明—— 解释 UE (左手系) 与 GIS/WGS84 (右手系) 转换逻辑。
- WebSocket 技术补充 —— 实时遥测数据包格式、心跳机制与双工通信规范。（AI提供，仅供参考）
- 系统架构设计

---
3. 核心模块分工 (Module Breakdown)
核心模块
协作人1
协作人2
协作人3
UE端
刘哲晗
杨璞
柯垣丞
后端
李昊泽
陈锦垚
关皓元

---
4. 六周开发计划（五周完成+一周冗余）
第一周：基础架构搭建 + 学习期
目标： 环境搭建、技术学习、基础通信框架
后端团队：
- 陈锦垚：
  - 学习后端框架（C++选型）
  - 搭建HTTP REST Server基础框架
  - 实现配置文件读取（config.yaml）
  - 完成注册管理模块的数据结构设计
- 关皓元：
  - 学习后端框架
  - 实现无人机注册接口（POST/GET /api/drones）
  - 实现持久化存储（JSON文件）
  - 完成ID↔编号映射逻辑
前端团队：
- 杨璞：
  - 学习UE5 HTTP模块和WebSocket模块
  - 封装HTTP Client基础类
  - 实现GET /api/drones轮询功能
  - 熟悉现有代码结构（DroneOpsPlayerController等）
- 刘哲晗：
  - 学习UE5 UMG Widget系统
  - 设计无人机注册表单UI原型
  - 学习Apifox等测试工具
  - 准备测试用例模板
- 柯垣丞：
  - 学习Cesium for UE5插件
  - 研究GPS坐标转换API
  - 阅读现有坐标转换代码（SimpleCoordinateService）
  - 准备Cesium场景测试环境
李昊泽（项目经理）：
- 确定技术选型（后端语言、测试工具）
- 搭建Mock Server（Apifox）供前端测试
- 协调团队学习进度

---
第二周：核心通信链路打通
目标： 前后端基础通信、UDP收发、坐标转换
后端团队：
- 陈锦垚：
  - 实现UDP遥测包接收（YAML解析）
  - 实现UDP控制包发送（24字节格式）
  - 完成坐标转换模块（NED↔UE偏移）
  - 实现GPS锚点记录逻辑
- 关皓元：
  - 完成无人机更新/删除接口（PUT/DELETE）
  - 实现GET /api/drones/{id}/anchor接口
  - 完成队列管理模块基础结构（独立队列）
  - 实现心跳维持逻辑（≥2Hz）
前端团队：
- 杨璞：
  - 完成WebSocket Client封装
  - 实现WebSocket连接/断线重连
  - 实现遥测数据接收与解析
  - 测试镜像机数据源切换（UDP→WebSocket）
- 刘哲晗：
  - 完成无人机注册表单UI实现
  - 实现无人机列表渲染Widget
  - 对接HTTP Client进行注册/查询
  - 编写前端测试用例
- 柯垣丞：
  - 实现Cesium GPS→UE世界坐标转换
  - 验证Cesium坐标系轴向对齐
  - 实现接收power_on事件触发锚定流程
  - 测试镜像机锚点设置
李昊泽：
- 维护Mock Server，添加WebSocket支持
- 协调前后端联调
- 记录技术问题和解决方案
- 使用Apifox测试所有已完成的HTTP接口
- 测试WebSocket连接

---
第三周：实时控制 + 状态管理
目标： 实时控制模式、连接状态机、弹窗系统
后端团队：
- 陈锦垚：
  - 实现连接状态机（离线→在线→失联）
  - 实现超时失联检测（10s）
  - 实现重连处理逻辑
  - 完成WebSocket状态事件推送
- 关皓元：
  - 实现WebSocket Server
  - 实现move指令处理
  - 实现pause/resume指令处理
  - 实现遥测推送（10Hz）
前端团队：
- 杨璞：
  - 实现控制指令发送（WebSocket move）
  - 实现暂停/恢复指令发送（P键）
  - 完成镜像机数据源完全切换到WebSocket
  - 实现多机选中（框选/Shift点击）
- 刘哲晗：
  - 实现弹窗通知系统核心（NotificationSubsystem）
  - 实现告警弹窗（低电量/断联）
  - 实现通用操作反馈弹窗（Toast）
  - 完成无人机删除/编辑UI
- 柯垣丞：
  - 实现断联重连锚点更新
  - 实现影子机初始位置同步
  - 测试锚定流程完整性
  - 协助杨璞测试多机选中
李昊泽：
- 实现测试接口（/api/debug/drone/{id}/state等）
- 编写测试脚本模拟无人机上线/断线
- 协调联调
测试负责人：刘哲晗&李昊泽
- 测试实时控制模式完整流程
- 测试状态机各状态转换
- 测试弹窗系统各类型弹窗

---
第四周：阵列执行 + 集结流程
目标： 阵列编辑、集结流程、执行引擎三种模式
后端团队：
- 陈锦垚：
  - 实现集结流程（ASSEMBLING状态）
  - 实现集结进度推送
  - 实现集结超时检测
  - 实现实时避障算法（基础版）
- 关皓元：
  - 实现侦察模式逻辑
  - 实现巡逻模式逻辑
  - 实现攻击模式逻辑
  - 实现阵列任务调度（多机并发）
前端团队：
- 杨璞：
  - 实现阵列任务下发（POST /api/arrays）
  - 实现接收集结状态反馈
  - 实现影子机集结期间行为（跟随镜像机）
  - 实现影子机与镜像机延迟计算
- 刘哲晗：
  - 实现集结进度弹窗
  - 实现集结超时弹窗
  - 实现集结完成弹窗
  - 优化弹窗系统（同类合并、原地刷新）
- 柯垣丞：
  - 实现航点编辑UI（点击添加、拖拽调整）
  - 实现执行模式配置UI（侦察/巡逻/攻击）
  - 实现阵列JSON序列化/持久化
  - 实现无人机槽位映射UI
李昊泽：
- 实现测试接口（/api/debug/cmd/{id}/array等）
- 编写多机并发测试脚本
- 协调阵列执行联调
测试负责人：刘哲晗&李昊泽
- 测试集结流程完整性
- 测试三种执行模式
- 测试多机并发调度

---
第五周：完善功能 + 集成测试
目标： 阵列预演、碰撞检测、告警系统、全流程测试
后端团队：
- 陈锦垚：
  - 优化实时避障算法
  - 实现告警推送（低电量/断联）
  - 优化坐标转换精度
  - 性能优化（心跳、遥测推送）
- 关皓元：
  - 实现POST /api/arrays/{id}/stop接口
  - 完善阵列任务状态管理
  - 实现到达判断阈值可配置
  - 修复已知Bug
前端团队：
- 杨璞：
  - 优化WebSocket重连逻辑
  - 优化镜像机插值移动平滑度
  - 完善多机选中交互
  - 修复已知Bug
- 刘哲晗：
  - 优化所有UI样式和交互
  - 完善告警弹窗去重逻辑
  - 编写完整测试用例文档
  - 执行全流程集成测试
- 柯垣丞：
  - 实现阵列预演动画
  - 实现碰撞检测（UE物理检测）
  - 实现阵列执行状态反馈UI
  - 完善航点编辑交互
李昊泽：
- 组织全流程集成测试
- 记录所有Bug和问题
- 协调Bug修复优先级
- 准备项目演示材料
测试负责人：刘哲晗&李昊泽
- 执行完整端到端测试
- 编写测试报告
- 验证所有需求完成度

---
第六周：冗余周（Bug修复 + 优化）
目标： 修复测试发现的问题、性能优化、文档完善
全体成员：
- 修复第五周测试发现的所有Bug
- 性能优化和稳定性提升
- 完善代码注释和文档
- 准备项目演示和答辩材料
李昊泽：
- 整理项目文档
- 准备演示PPT
- 组织预演
- 收集团队反馈
测试负责人：刘哲晗&李昊泽
- 回归测试
- 验证Bug修复
- 编写最终测试报告

---
关键调整说明：
1. 测试专人： 指定刘哲晗为测试负责人，从第二周开始专职测试，同时负责UI开发（UI开发工作量相对较小）
2. 工作量平衡：
  - 李昊泽作为项目经理，主要负责Mock Server、测试接口、协调工作，不承担核心开发
  - 陈锦垚和关皓元后端工作量均衡分配
  - 杨璞负责通信模块（最复杂），柯垣丞负责Cesium和阵列编辑
  - 刘哲晗负责UI+测试（UI相对简单，有时间做测试）
3. 学习曲线： 第一周专门留出学习时间，所有人从零学起UE5/后端框架/测试工具
4. 风险缓冲： 第六周作为完整冗余周，确保有时间修复问题
5. 里程碑验证： 每周末进行周会，验证本周目标完成度，及时调整下周计划

---
# 第一次新增待实现需求

以下需求来自三个关卡整合阶段（主菜单 + 队列编辑 + 预演），包含 UI 与非 UI 部分。

**分工总览**

| 负责人 | 负责模块 | 工作量 |
|--------|----------|--------|
| 柯垣丞 | 主菜单关卡（WBP_MainMenu 全部区域）、队列编辑保存命名弹窗（WBP_SaveNameDialog）、队列编辑返回按钮 | 主力 |
| 杨璞 | 预演关卡：Shift 鼠标交互（C++）、常驻无人机状态面板（WBP_DroneStatusPanel）、返回主菜单按钮 | 中等 |
| 李昊泽 | 预演关卡：阵列播放模块（WBP_ArrayPlayback，仅 UI 布局和槽位映射逻辑）、通信协议 `type→mode` 字段变更（前端侧） | 轻量 |

> 集结弹窗（WBP_AssemblyProgress）和告警弹窗已有现成基类（`UAssemblyPopupWidget` / `UToastWidget`），建议柯垣丞在主菜单完成后接手，或由杨璞在状态面板完成后接手，视进度决定。

---

### 一、通信协议变更（前后端均需修改）【前端：李昊泽 / 后端：各自负责】

**背景**：当前 UE5→后端控制指令中 `type` 字段语义不清，无法区分移动指令与任务模式。

**变更内容**：
- 将控制指令中的 `type` 字段重命名为 `mode`
- `mode` 枚举值扩展为以下四种：
  - `move`：普通移动指令（原有行为不变）
  - `scout`：侦察模式
  - `patrol`：巡逻模式
  - `attack`：攻击模式

**影响范围**：
- 后端：解析控制指令时将 `type` 改为 `mode`，字段含义不变，仍需接收目标坐标 `x/y/z`
- 前端（UE5）：`DroneCommandSenderComponent` 发送指令时将字段名改为 `mode`，并在 UI 中提供模式切换入口

**当前阶段实现约定**：
- 四种模式（`move` / `scout` / `patrol` / `attack`）的后端执行逻辑暂时相同，均按 move 处理（移动到目标坐标）
- 模式字段仅作为标记保留，供后续扩展差异化行为
- 单点指令（点选无人机派发指令）和阵列播放派发指令均使用此格式

**指令格式**（更新后）：
```json
{
  "mode": "move",
  "drone_id": "d1",
  "x": 1000.0,
  "y": 2000.0,
  "z": 500.0
}
```

**负责人**：前后端各自负责自己侧的修改，需联调确认字段对齐

---

### 二、主菜单关卡（新关卡：MainMenu）【负责人：柯垣丞】

**关卡类型**：纯 2D UI，无 3D 场景，无 Pawn

**C++ 框架已完成**：`AMainMenuGameMode`、`AMainMenuPlayerController`、`UMainMenuWidget`（位于 `Source/UE5DroneControl/MainMenu/`）

#### 2.1 Widget：WBP_MainMenu

**父类**：`UMainMenuWidget`

**布局区域**：

**区域 0：项目标题**
- 一个 Text 控件，显示项目名称（如"无人机数字孪生平台"），居中置顶

**区域 A：无人机注册模块**

| 控件 | 类型 | 说明 |
|------|------|------|
| DroneId 输入框 | UEditableTextBox | 输入整数，如 1、2 |
| IP 地址输入框 | UEditableTextBox | 输入字符串，如 192.168.30.104 |
| 注册按钮 | UButton | OnClicked → 调用 `RegisterDrone(DroneId, IpAddress)` |
| 已注册列表 | UScrollBox | 每行显示 `ID:X  Name:UAV-X`，行末有删除按钮 |
| 删除按钮（每行） | UButton | OnClicked → 调用 `UnregisterDrone(DroneId)` |

事件响应：
- 重写 `OnDroneRegistered(DroneId)` → 调用 `GetRegisteredDronesSummary()` 刷新列表
- 重写 `OnDroneUnregistered(DroneId)` → 同上

**区域 B：基础设置模块**

| 控件 | 类型 | 说明 |
|------|------|------|
| 后端 WebSocket 地址 | UEditableTextBox | 默认 `ws://192.168.30.104:8080` |
| UE 端接收端口 | UEditableTextBox | 默认 `8888`，整数 |
| 预演模块中心纬度 | UEditableTextBox | 浮点数 |
| 预演模块中心经度 | UEditableTextBox | 浮点数 |
| 预演模块中心海拔 | UEditableTextBox | 单位：米，WGS84 椭球高，默认 43 |
| 保存按钮 | UButton | OnClicked → 调用 `SaveSettings(...)`，设置立即对预演关卡生效 |

初始化：Widget Construct 事件中调用 `LoadSettings(...)` 填充各输入框（不保存则使用当前默认值）。

事件响应：重写 `OnSettingsSaved()` → 显示 Toast 提示"设置已保存"。

**区域 C：导航按钮**

| 控件 | 类型 | 说明 |
|------|------|------|
| 队列编辑按钮 | UButton | OnClicked → 调用 `OnGoToQueueEditorClicked()` |
| 预演按钮 | UButton | OnClicked → 调用 `OnGoToPreviewClicked()` |

---

### 三、队列编辑关卡（RuntimeInteraction）新增需求【负责人：柯垣丞】

#### 3.1 保存时命名弹窗

- 用户点击"保存"按钮后，弹出命名弹窗（`WBP_SaveNameDialog`）
- 弹窗包含：文本输入框（支持键盘输入）、确认按钮、取消按钮
- 确认后以输入的名称作为文件名保存 JSON 到 `Saved/DronePaths/` 目录
- 取消则关闭弹窗，不保存
- 文件名不允许为空，为空时确认按钮置灰或显示提示文字
- 文件名需校验非法字符，Windows 路径不允许 `\ / : * ? " < > |`，检测到非法字符时提示用户并阻止保存

#### 3.2 返回主菜单按钮

- 常驻悬浮按钮，固定在屏幕角落（建议左上角）
- 显示文字"返回主菜单 [B]"
- OnClicked → 调用 `ADroneRuntimeInteractionPlayerController::ReturnToMainMenu()`
- B 键的 C++ 绑定已完成，按钮为视觉辅助

---

### 四、预演关卡（CesiumWorld / DroneOps）新增需求

#### 4.1 鼠标交互规则【负责人：杨璞（C++ 实现）】

- 默认状态：隐藏鼠标光标，游戏输入模式（Game Only）
- 长按 Shift 键：显示鼠标光标，切换为 Game And UI 输入模式
  - 此时鼠标悬停在 UI 上：点击操作 UI 控件（UI 层遮挡无人机，不会误触无人机）
  - 此时鼠标悬停在场景中（无 UI 遮挡）：点击无人机执行多选逻辑（与原有 Shift+点击多选行为一致）
- 松开 Shift 键：恢复隐藏光标，切换回 Game Only 输入模式

**C++ 实现位置**：`ADroneOpsPlayerController::Tick` 中检测 `bShiftHeld` 状态变化，调用 `SetInputMode(FInputModeGameAndUI)` / `SetInputMode(FInputModeGameOnly)` 切换，同时控制 `bShowMouseCursor`。

#### 4.2 常驻无人机状态面板（WBP_DroneStatusPanel）【负责人：杨璞】

显示当前所有已注册无人机的状态，常驻屏幕（建议右侧竖排列表）。

每行显示一架无人机，包含：

| 字段 | 说明 |
|------|------|
| ID | 无人机编号 |
| 名称 | 如 UAV-1 |
| 连接状态 | 在线 / 失联 / 断联（对应 EDroneAvailability） |
| 当前移动模式 | 侦察 / 巡逻 / 攻击，可点击切换 |

模式切换逻辑：
- 点击模式标签后循环切换三种模式（或弹出下拉选择）
- 切换后记录该无人机的当前模式，后续通过 UE 点选派发单点指令时，`mode` 字段使用该模式值
- 当前阶段四种模式执行逻辑相同（均执行 move），模式字段仅作标记
- 依赖通信协议变更（见第一节）

**注意**：模式切换本身不立即向后端发送指令，仅更新本地状态；下次点选无人机派发移动指令时才携带该模式值。

数据来源：订阅 `UDroneRegistrySubsystem::OnTelemetryUpdated` 事件刷新显示，与 `WBP_DroneInfoPanel` 显示字段保持一致。

#### 4.3 阵列播放模块（WBP_ArrayPlayback）【负责人：李昊泽】

入口：预演关卡内的一个按钮或常驻面板区域。

**步骤 1：选择阵列文件**
- 列出 `Saved/DronePaths/` 目录下所有 JSON 文件
- 用户点击选中某个文件

**步骤 2：无人机槽位映射**
- 读取 JSON 中需要的无人机数量和编号
- 为每个"阵列无人机槽位"提供一个下拉菜单，选项为当前场景中已注册的真实无人机
- 约束：每架真实无人机只能被映射到一个槽位（已选的从其他下拉菜单中移除）
- 提供"一键自动对应"按钮：系统随机将已注册无人机与槽位一一对应

**步骤 3：移动模式设置**
- 提供模式选择控件（下拉或三选一按钮组）：侦察 / 巡逻 / 攻击
- 此模式将作为 `mode` 字段随阵列指令一起下发
- 当前阶段三种模式执行逻辑与 move 相同，字段仅作标记

**步骤 4：执行方式选择**

| 按钮 | 行为 |
|------|------|
| 本地预演 | 仅在 UE 端渲染动画：影子机按 JSON 中设定的速度和点位信息在本地执行飞行动画，不向后端发送任何指令，镜像机保持当前位置不动 |
| 派发指令 | 向后端发送阵列任务并触发集结流程（见 4.5）；集结完成后，影子机按 JSON 点位和速度在本地执行预演动画，镜像机同时根据后端 WebSocket 推送的遥测数据实时更新位置；两者独立运动，不强制位置重合 |

#### 4.4 返回主菜单按钮【负责人：杨璞】

- 长按 Shift 显示鼠标后可见此按钮（或始终显示，不受 Shift 影响，由 UI 程序员决定）
- 显示文字"返回主菜单 [B]"
- OnClicked → 调用 `ADroneOpsPlayerController::OnReturnToMainMenu()`
- B 键的 C++ 绑定已完成

#### 4.5 集结流程弹窗（WBP_AssemblyProgress）【负责人：视进度由柯垣丞或杨璞接手】

触发条件：用户在阵列播放模块选择"派发指令"后触发。

弹窗内容：
- 标题："集结中..."
- 每架无人机的集结状态列表（已到位 / 集结中 / 超时）
- 整体进度条（已到位数 / 总数）
- 集结完成后自动关闭或显示"开始执行"按钮
- 集结超时时显示警告并提供"强制开始"或"取消"选项

数据来源：订阅后端通过 WebSocket 推送的集结进度事件。

现有 `UAssemblyPopupWidget` 可作为基础扩展。

#### 4.6 告警弹窗【负责人：视进度由柯垣丞或杨璞接手】

触发条件：后端通过 WebSocket 推送告警信息（低电量、断联、碰撞预警等）。

弹窗要求：
- 非阻塞式，不打断操作
- 显示告警类型、涉及无人机 ID、时间戳
- 同类告警合并显示（不重复堆叠）
- 可手动关闭

现有 `UToastWidget` 可用于轻量提示，严重告警需独立弹窗。

---

### 五、待办事项（非 UI，需后续跟进）

1. `FDroneDescriptor` 目前无 IP 地址字段，如需持久化注册信息中的 IP，需在 `DroneOpsTypes.h` 中扩展该结构体
2. `SaveSettings` / `LoadSettings` 目前为占位实现，需在 GameInstance 或 `USaveGame` 中实现真正的跨关卡持久化
3. 关卡名称（`MainMenuLevelName`、`QueueEditorLevelName`、`PreviewLevelName`）需在蓝图子类中与实际 `.umap` 文件名对齐
4. Shift 键显示/隐藏鼠标的输入模式切换逻辑需在 `ADroneOpsPlayerController` 中实现：`bShiftHeld` 状态已有，需在 Tick 中检测状态变化并调用 `SetInputMode` + `bShowMouseCursor` 切换
5. 通信协议 `type` → `mode` 字段变更需前后端同步修改并联调验证

---

附加需求：Cesium 场景地面海拔配置

背景：
Cesium 的 CesiumGeoreference Origin Height 决定了 UE5 世界坐标 Z=0 对应的 WGS84 椭球高度。
项目共有约 4 个关卡，每个关卡对应不同地理位置，地面海拔不同，需要在进入关卡前配置正确的海拔值，否则镜像机锚点 Z 坐标会偏入地面以下。

需求描述：
- 在主关卡（Main Level）中提供一个 UI 输入框，允许用户在进入 CesiumWorld 关卡前输入当前测试地点的地面海拔高度（单位：米，WGS84 椭球高）
- 用户确认后，将该值写入 CesiumGeoreference 的 Origin Height，然后加载对应关卡
- 该值在本次运行期间保持不变（不需要持久化到磁盘）
- 每次进入关卡前必须重新设置，不沿用上次的值

实现要点：
- UI：主关卡中的 Widget，包含一个数字输入框（默认值可填 43，对应北京平均海拔）和"进入关卡"按钮
- 逻辑：按钮点击后调用 `ACesiumGeoreference::SetOriginHeight(value)`，然后执行关卡切换
- 负责人：柯垣丞
- 所属周次：第四周（与阵列编辑 UI 同期实现）

---

附加需求：无人机注册队列 UI（WBP_DroneRegistrationQueue）

背景：
主菜单中已有基础注册模块说明，但当前 C++ 的 `UMainMenuWidget::RegisterDrone()` 只把无人机写入 UE 本地 `UDroneRegistrySubsystem`，不会调用后端 `POST /api/drones`。实际联调时，无人机注册、编辑、删除必须以 C++ 后端为准，UE UI 通过 HTTP 接口读写后端注册队列。

#### Widget：WBP_DroneRegistrationQueue

**用途**：在主菜单或独立注册页面中管理后端无人机注册队列，支持查看、注册、编辑、删除、刷新。

**父类**：建议新增 `UDroneRegistrationQueueWidget : UUserWidget`；如果时间不足，可先用 `UUserWidget` 蓝图实现界面，但 HTTP 调用建议封装到 C++ 基类中，避免蓝图里手写 JSON。

**后端地址**：
- HTTP BaseUrl 使用 `UDroneNetworkManager::BackendBaseUrl`，默认 `http://127.0.0.1:8080`
- 不要使用旧 demo 地址 `http://localhost:8000/api/drones`

**布局区域**：

**区域 A：注册表单**

| 控件 | 类型 | 说明 |
|------|------|------|
| Name 输入框 | UEditableTextBox | 无人机名称，如 `UAV1`；必填 |
| Model 输入框 | UEditableTextBox | 型号，可为空 |
| Slot 输入框/下拉框 | UEditableTextBox 或 UComboBoxString | 槽位编号，对应后端 `slot`，建议限制为 1~6 |
| IP 输入框 | UEditableTextBox | Jetson/无人机 IP；为空时后端使用配置默认值 |
| Port 输入框 | UEditableTextBox | 控制发送端口；可为空，后端按 `port_map` 自动填充 |
| VideoUrl 输入框 | UEditableTextBox | 视频流地址，可为空 |
| 注册按钮 | UButton | OnClicked → 调用 `CreateDroneRegistration(...)` |
| 清空按钮 | UButton | 清空表单输入，不发送请求 |

注册按钮逻辑：
- 点击前校验 `Name` 非空、`Slot` 是正整数
- 拼 JSON：

```json
{
  "name": "UAV1",
  "model": "PX4",
  "slot": 1,
  "ip": "192.168.30.104",
  "port": 8889,
  "video_url": ""
}
```

- 调用：

```cpp
UDroneNetworkManager* NetMgr = GetGameInstance()->GetSubsystem<UDroneNetworkManager>();
UDroneHttpClient* Http = NetMgr->GetHttpClient();
Http->Post(TEXT("/api/drones"), JsonBody, Callback);
```

- 成功后刷新列表：调用 `RefreshDroneQueue()`
- 失败时显示 Toast：使用后端返回的错误 body，例如重名、槽位冲突、超过最大数量

**区域 B：已注册队列列表**

| 控件 | 类型 | 说明 |
|------|------|------|
| DroneScrollBox | UScrollBox | 每行一个无人机注册项 |
| 刷新按钮 | UButton | OnClicked → 调用 `RefreshDroneQueue()` |
| 空状态文本 | UTextBlock | 当列表为空时显示"暂无已注册无人机" |
| 加载状态 | UTextBlock 或 Loading 图标 | 请求中显示，防止重复点击 |

每行建议做成独立 Widget：`WBP_DroneRegistrationRow`

| 字段/按钮 | 类型 | 说明 |
|------|------|------|
| ID | UTextBlock | 显示后端返回的 `id`，如 `1` |
| ID_STR | UTextBlock | 显示 `id_str`，如 `d1` |
| 名称 | UTextBlock | `name` |
| 型号 | UTextBlock | `model` |
| 槽位 | UTextBlock | `slot` |
| IP:Port | UTextBlock | `ip` + `port` |
| 状态 | UTextBlock | `status`：offline / connecting / online / lost |
| 电量 | UTextBlock | `battery`，-1 显示为未知 |
| 位置 | UTextBlock | `x/y/z`，可选显示 |
| 编辑按钮 | UButton | 打开编辑模式或弹窗 |
| 删除按钮 | UButton | 二次确认后调用删除接口 |

刷新列表逻辑：
- 调用：

```cpp
Http->Get(TEXT("/api/drones"), Callback);
```

- 后端返回是顶层 JSON 数组，不是 `{ "drones": [...] }`
- 需要解析字段：

| JSON 字段 | UI 用途 |
|------|------|
| `id` | 后续 PUT/DELETE 使用的主键 |
| `id_str` | 可显示给用户 |
| `name` | 名称 |
| `model` | 型号 |
| `slot` / `slot_number` | 槽位 |
| `ip` | IP |
| `port` | 端口 |
| `video_url` | 视频流 |
| `status` | 连接状态 |
| `battery` | 电量 |
| `x/y/z` | UE 坐标 |
| `yaw` | 朝向 |
| `speed` | 速度 |
| `ue_receive_port` | UE 接收端口 |
| `topic_prefix` | ROS topic 前缀 |
| `bit_index` | 多机控制 bit 位 |
| `mavlink_system_id` | MAVLink system id |

状态显示规则：
- `online`：绿色，显示"在线"
- `connecting`：黄色，显示"连接中"
- `lost`：红色，显示"失联"
- `offline`：灰色，显示"离线"
- `battery == -1`：显示"未知"，不要显示为 -1%

**区域 C：编辑弹窗/行内编辑**

编辑时允许修改：

| 字段 | 是否允许修改 |
|------|------|
| name | 允许 |
| model | 允许 |
| ip | 允许 |
| port | 允许 |
| video_url | 允许 |
| id / id_str / slot | 不建议在 UI 中修改 |

保存编辑逻辑：
- 调用：

```cpp
const FString Path = FString::Printf(TEXT("/api/drones/%d"), DroneId);
Http->Put(Path, JsonBody, Callback);
```

- 成功后调用 `RefreshDroneQueue()`
- 失败时显示错误提示

**区域 D：删除确认**

删除按钮逻辑：
- 点击每行删除按钮后弹出确认框，显示"确认删除 UAV1 吗？"
- 确认后调用：

```cpp
const FString Path = FString::Printf(TEXT("/api/drones/%d"), DroneId);
Http->Delete(Path, Callback);
```

- 成功后调用 `RefreshDroneQueue()`
- 失败时显示错误提示；后端 404 表示该无人机已不存在，应刷新列表

#### 建议新增 C++ 基类函数

建议在 `Source/UE5DroneControl/UI/` 下新增：
- `DroneRegistrationQueueWidget.h/.cpp`
- `DroneRegistrationRowWidget.h/.cpp`

`UDroneRegistrationQueueWidget` 建议暴露给蓝图的函数：

```cpp
UFUNCTION(BlueprintCallable, Category = "DroneRegistration")
void RefreshDroneQueue();

UFUNCTION(BlueprintCallable, Category = "DroneRegistration")
void CreateDroneRegistration(const FString& Name, const FString& Model, int32 Slot, const FString& Ip, int32 Port, const FString& VideoUrl);

UFUNCTION(BlueprintCallable, Category = "DroneRegistration")
void UpdateDroneRegistration(int32 DroneId, const FString& Name, const FString& Model, const FString& Ip, int32 Port, const FString& VideoUrl);

UFUNCTION(BlueprintCallable, Category = "DroneRegistration")
void DeleteDroneRegistration(int32 DroneId);

UFUNCTION(BlueprintImplementableEvent, Category = "DroneRegistration")
void OnDroneQueueLoaded(const TArray<FDroneRegistrationViewData>& Drones);

UFUNCTION(BlueprintImplementableEvent, Category = "DroneRegistration")
void OnDroneQueueRequestFailed(const FString& ErrorMessage);

UFUNCTION(BlueprintImplementableEvent, Category = "DroneRegistration")
void OnDroneQueueMutated();
```

建议新增蓝图可读结构体：

```cpp
USTRUCT(BlueprintType)
struct FDroneRegistrationViewData
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly) int32 Id = 0;
    UPROPERTY(BlueprintReadOnly) FString IdStr;
    UPROPERTY(BlueprintReadOnly) FString Name;
    UPROPERTY(BlueprintReadOnly) FString Model;
    UPROPERTY(BlueprintReadOnly) int32 Slot = 0;
    UPROPERTY(BlueprintReadOnly) FString Ip;
    UPROPERTY(BlueprintReadOnly) int32 Port = 0;
    UPROPERTY(BlueprintReadOnly) FString VideoUrl;
    UPROPERTY(BlueprintReadOnly) FString Status;
    UPROPERTY(BlueprintReadOnly) int32 Battery = -1;
    UPROPERTY(BlueprintReadOnly) FVector WorldLocation = FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly) float Yaw = 0.0f;
    UPROPERTY(BlueprintReadOnly) float Speed = 0.0f;
    UPROPERTY(BlueprintReadOnly) int32 UEReceivePort = 0;
    UPROPERTY(BlueprintReadOnly) FString TopicPrefix;
    UPROPERTY(BlueprintReadOnly) int32 BitIndex = 0;
    UPROPERTY(BlueprintReadOnly) int32 MavlinkSystemId = 0;
};
```

#### 与现有系统的关系

- `GET /api/drones` 的轮询已在 `UDroneNetworkManager::PollDroneList()` 中存在，但该函数是 private，UI 不直接调用它
- UI 主动刷新应通过 `UDroneNetworkManager::GetHttpClient()->Get(TEXT("/api/drones"), Callback)` 调用
- `UDroneNetworkManager::SyncDroneListToRegistry()` 目前只同步静态描述信息到 `UDroneRegistrySubsystem`，不保存 `status/battery/x/y/z/yaw/speed`
- 注册队列 UI 如需显示状态、电量和位置，必须直接解析 `GET /api/drones` 响应，不能只读 `GetRegisteredDronesSummary()`
- `UMainMenuWidget::RegisterDrone()` 当前只注册 UE 本地 Registry，不等价于后端注册；本需求中的注册按钮必须调用 `POST /api/drones`

#### 验收标准

1. 后端启动后，打开 UI 自动调用 `GET /api/drones` 并正确渲染顶层数组
2. 新增无人机后，后端 `BackEnd/data/drones.json` 中出现对应记录，列表自动刷新
3. 重名或重复 slot 时，UI 显示后端错误提示，不新增假数据
4. 编辑名称/IP/端口后，重新刷新列表能看到新值
5. 删除无人机后，列表中该行消失，再次刷新仍不存在
6. 后端未启动时，UI 不崩溃，显示"后端连接失败"或等价提示
7. 不再使用 `localhost:8000` 旧 demo 地址

---
# 第二次新增待实现需求

来源：外场测试结束后归纳的新增功能与已知问题修复。

**分工总览**

| 负责人 | 负责模块 | 工作量 |
|--------|----------|--------|
| 柯垣丞 | 需求4（影子机手动重合 R 键）、需求6（编辑关卡阵列播放UI+功能）、需求7（编辑关卡保存路径修复） | 主力 |
| 杨璞 | 需求1（通讯协议+前端匈牙利自动分配）、需求5（追随视角修复）、需求10（影子机同步渲染移动） | 主力 |
| 关皓元 | 需求1后端部分（匈牙利算法+新字段支持）、需求2后端部分（标识统一）、 | 主力 |
| 刘哲晗 | 需求9（UI美化，使用项目内现有资产） | 主力 |
| 李昊泽 | 无人机无关功能的全流程测试（详见测试清单）、需求8（离线地图.ini切换） | 主力 |

---

### 一、通讯协议变更：支持匈牙利算法自动分配【前端：杨璞 / 后端：关皓元】

**背景**：当前阵列派发需要用户手动在 `WBP_ArrayPlayback` 的下拉菜单中为每个路径槽位指定真实无人机。引入后端匈牙利算法后，用户可勾选"自动分配"，由后端根据无人机当前位置与航点起始位置自动完成最优匹配，并将分配结果回传前端同步显示。

#### 1.1 通讯协议变更（由架构师完成）

`POST /api/arrays` 请求体新增字段 `auto_assign`：

```json
{
  "array_id": "a1",
  "mode": "scout",
  "auto_assign": true,
  "paths": [
    {
      "path_id": 1,
      "drone_id": "",
      "waypoints": [...]
    }
  ]
}
```

- `auto_assign: true`：`drone_id` 字段留空或忽略，后端执行匈牙利算法自动分配
- `auto_assign: false`（或字段缺省）：行为与当前一致，`drone_id` 必须显式指定

后端新增 WebSocket 推送消息类型 `assignment_result`，在匈牙利算法完成后立即推送（先于集结流程开始）：

```json
{
  "type": "assignment_result",
  "array_id": "a1",
  "assignments": [
    { "path_id": 1, "drone_id": "d1" },
    { "path_id": 2, "drone_id": "d3" }
  ]
}
```

#### 1.2 后端实现【关皓元】

- 在 `POST /api/arrays` 处理逻辑中，读取 `auto_assign` 字段
- 若为 `true`，从 `DroneManager` 获取当前所有在线无人机的 NED 位置，从 `paths` 中提取各路径第一个航点的 UE 坐标（转换为 NED），构造代价矩阵，执行匈牙利算法，输出最优 `drone_id ↔ path_id` 映射
- 分配完成后，通过 `WsManager::broadcast` 推送 `assignment_result` 消息
- 后续集结流程与当前一致，使用算法分配结果中的 `drone_id` 替换原路径的 `drone_id`

#### 1.3 前端实现【杨璞】

- 在 `WBP_ArrayPlayback`（预演关卡的阵列播放面板）的"无人机槽位映射"区域（步骤2）增加"自动分配"勾选框（CheckBox）
- 勾选"自动分配"后：
  - 手动下拉选择框置灰，不允许手动指定
  - 发送 `POST /api/arrays` 时 `auto_assign: true`，`paths[].drone_id` 留空
- 接收到 `assignment_result` 推送后，将分配结果同步显示到对应槽位的下拉框（展示分配了哪架无人机），此时槽位仍为只读
- 接收 `assignment_result` 的解析逻辑建议放在 `UDroneNetworkManager::OnWsMessage` 中，通过新事件 `OnAssignmentResult` 广播给 UI

---

### 二、无人机标识统一【前端：杨璞 / 后端：关皓元】

**背景**：当前系统中同时存在 `id`（数字）、`id_str`（"d1"）、`name`（"UAV1"）、`slot`（物理槽位）四套标识，UI 和日志中混用，外场测试时造成混乱。

**统一方向**：
- **删除无人机名称（name）字段**：注册时不再要求填写名称，UI 上所有显示"名称"的地方改为显示无人机 ID（`id_str`，格式为 "d1"/"d2"）
- **slot 字段保留但不在 UI 中主动暴露**：slot 是后端内部端口映射所需的物理槽位编号，注册时前端仍需传递，但改为在注册表单中作为"槽位编号"填写，不再叫"名称"；列表展示中不单独显示 slot 列
- **统一显示规则**：UI 所有展示无人机身份的地方，只显示 `id_str`（如 "d1"），不再显示 name

**前端变更【杨璞】**：
- 注册表单删除 `Name` 输入框，或将其改为可选的备注字段（不参与后端注册 JSON，仅本地显示）
- `WBP_DroneRegistrationRow`、`WBP_DroneStatusPanel`、`WBP_DroneInfoPanel` 等所有列表/面板中，"名称"列改为显示 `id_str`
- 阵列播放槽位映射下拉框选项文字改为 `id_str`

**后端变更【关皓元】**：
- `POST /api/drones` 的 `name` 字段改为可选（缺省时后端自动用 `id_str` 填充，如 "d1"）
- `GET /api/drones` 返回的 `name` 字段保留但内容与 `id_str` 相同（向后兼容，避免前端解析报错）

---

### 三、注册 UI 精简（此需求先不实现）

**背景**：当前注册表单包含 IP、Port、VideoUrl 等字段，与后端实际注册逻辑不符（后端通过 `config.yaml` 中的 `port_map` 决定 UDP 端口，IP 字段当前未被使用）。外场测试时多余字段造成混淆。

**变更内容**：

注册表单仅保留以下字段：

| 字段 | 类型 | 说明 |
|------|------|------|
| 槽位编号（slot） | 数字输入框 | 必填，1~6，对应 config.yaml 中的 port_map |

删除以下 UI 字段：
- IP 地址输入框
- Port 输入框
- VideoUrl 输入框
- Name 输入框（见需求二）

注册时发送的 JSON 仅包含：
```json
{ "slot": 1 }
```

**注意**：后端注册接口 `POST /api/drones` 的 IP/Port/Name 字段仍保留（供未来扩展），前端只是不再在 UI 中暴露这些字段。

**验收标准**：注册表单只有"槽位编号"一个输入框和"注册"按钮；注册成功后列表刷新显示新条目，显示 `id_str` 和 `slot`。

---

### 四、影子机与镜像机手动重合（R 键）【负责人：柯垣丞】

**背景**：当前影子机（DronePathActor）与镜像机（RealTimeDroneReceiver）重合逻辑依赖集结流程，外场测试时需要快速手动校准两者位置。

**需求描述**：在预演关卡按下 R 键，将所有影子机的位置强制重置到对应镜像机的当前位置。

**实现位置**：`ADroneOpsPlayerController`

**逻辑**：
1. 收到 R 键输入
2. 遍历场景中所有已注册的 `ARealTimeDroneReceiver`，取其当前 `WorldLocation`
3. 找到对应 `DroneId` 的影子机 Actor（`ADronePathActor` 或 `AMultiDroneCharacter`，视影子机实现而定），将其 `SetActorLocation` 到镜像机位置
4. 若影子机正在执行预演动画，停止动画后再重置位置

**输入绑定**：在 `ADroneOpsPlayerController::SetupInputComponent` 中绑定 R 键到新函数 `ResetShadowDronesToMirrors()`。

---

### 五、追随视角修复【负责人：杨璞】

**问题描述**：在预演关卡中，从自由视角（F 键）切回追随视角时，当前逻辑总是切换到编号为 d1 的无人机视角，而不是返回到用户上次手动选择的无人机视角。

**修复逻辑**：
- 在 `ADroneOpsPlayerController` 中新增成员变量 `LastFollowedDroneId`，初始值为 -1（无效）
- 每次用户通过 0 键或 1 键主动切换追随某架无人机时，更新 `LastFollowedDroneId`
- 按 F 键切回追随视角时，优先恢复 `LastFollowedDroneId` 对应的无人机视角；若 `LastFollowedDroneId == -1`（从未手动选择过），则维持当前行为（切换到第一架在线无人机）

---

### 六、编辑关卡阵列播放功能【负责人：柯垣丞】

**背景**：当前编辑关卡（RuntimeInteraction）只支持航点编辑和保存，不支持直接播放已保存的阵列路径。需要新增类似预演关卡的阵列播放功能，但逻辑更简单（不需要集结，以第一架无人机为锚点直接重置并播放）。

#### 6.1 UI：新增阵列播放面板

在编辑关卡 HUD 中新增一个面板（`WBP_EditorArrayPlayback`），包含：

| 控件 | 说明 |
|------|------|
| 文件列表 | 列出 `Saved/DronePaths/` 目录下所有 JSON 文件，点击选中 |
| 刷新按钮 | 重新扫描目录 |
| 播放按钮 | 加载选中文件并播放 |
| 停止按钮 | 停止当前播放 |

#### 6.2 播放逻辑

1. 读取选中的 JSON 文件，解析为 `FDronePathsSaveData`（复用现有 `UDronePathSaveLibrary`）
2. 以**第一架无人机的第一个航点**为锚点，其他无人机以此锚点确定自己的
3. **重置所有影子机位置**为各自路径的初始航点位置
4. 调用 `ADronePlaybackManager::PlayFromData(SaveData)` 开始本地预演动画
5. 播放完成后影子机停在路径终点

**注意**：编辑关卡没有集结流程，不向后端发送任何指令，纯本地播放。

---

### 七、编辑关卡保存路径修复【负责人：柯垣丞】

**问题描述**：编辑关卡保存航点路径时，文件无法正确保存到 `Saved/DronePaths/` 路径，疑似路径拼接或目录创建逻辑有误。

**排查方向**：
- 检查 `UDronePathSaveLibrary::SavePathsToFile` 中路径拼接逻辑，确认使用 `FPaths::ProjectSavedDir() + "DronePaths/"` 而非相对路径
- 确认目录不存在时会自动创建（`IFileManager::Get().MakeDirectory`）
- 在保存成功/失败时打印详细日志（保存路径、错误信息）

**验收标准**：在编辑关卡编辑航点并点击保存后，在 `{ProjectRoot}/Saved/DronePaths/` 目录下可以找到对应的 JSON 文件。

---

### 八、离线 Cesium 地图【负责人：李昊泽】

**背景**：外场测试时局域网内无法访问 Google Maps / Cesium ion API，导致地图瓦片无法加载。需要预先下载地图瓦片，在本地启动一个 HTTP tile server，UE5 通过局域网访问本地服务。

#### 8.1 部署方案

- 本地 tile server 与 UE5 同机运行（Windows），UE5 通过 `http://localhost:{port}` 访问
- 推荐工具：`tileserver-gl`（Node.js）或 `mbtiles` + 简单 HTTP 服务，预先下载目标区域的 `.mbtiles` 瓦片包

#### 8.2 UE5 侧配置【柯垣丞】

在 `Config/DefaultEngine.ini`（或项目自定义 ini）中新增配置项：

```ini
[CesiumTileServer]
UseLocalTileServer=true
LocalTileServerUrl=http://localhost:8070
```

- `UseLocalTileServer=true` 时，Cesium for UE 的 URL 模板替换为本地地址
- `UseLocalTileServer=false` 时，使用原有在线 Cesium ion / Google Maps API（默认行为）
- 修改 `.ini` 文件后**重启 UE5**（或实现运行时读取）即可生效，无需重新打包

**实现位置**：在 `ACesiumGeoreference` 初始化流程或 `ADroneOpsGameMode::BeginPlay` 中，读取上述 ini 配置，若 `UseLocalTileServer=true` 则动态修改 Cesium 的 tile URL。

**注意**：tile server 的启动与维护属于部署任务，不在 UE5 代码范围内；此需求的 UE5 代码任务仅为实现 ini 驱动的 URL 切换。

---

### 九、UI 美化【负责人：刘哲晗】

**背景**：项目内已放置 UI 资产（图标、材质、字体等），需要基于现有资产对各关卡 UI 进行美化。

**范围**：
- 主菜单关卡（WBP_MainMenu）
- 预演关卡 HUD（WBP_DroneStatusPanel、WBP_ArrayPlayback、WBP_AssemblyProgress）
- 编辑关卡 HUD（WBP_EditorArrayPlayback、保存命名弹窗）

**原则**：只使用项目内已有资产，不引入新的外部资产；功能逻辑不变，只调整视觉样式。

---

### 十、预演关卡影子机同步渲染移动【负责人：杨璞】

**需求描述**：在预演关卡中，当用户触发阵列播放（本地预演或派发指令均可），影子机应当渲染飞行动画。具体行为如下：

| 场景 | 影子机行为 | 镜像机行为 |
|------|-----------|-----------|
| 仅本地预演（无人机未连接） | 按 JSON 路径渲染飞行动画，播放完停在终点 | 保持当前位置不动 |
| 派发指令（无人机已连接） | 按 JSON 路径同步渲染飞行动画 | 同时根据后端 WebSocket 遥测数据实时更新位置，两者独立运动 |
| 派发指令（无人机未连接，纯测试） | 按 JSON 路径渲染飞行动画，播放完停在终点 | 保持不动（无遥测数据） |

**实现要点**：
- 影子机播放由 `ADronePlaybackManager::PlayFromData` 驱动，与镜像机逻辑完全解耦
- 判断"无人机是否连接"的依据：对应 `ARealTimeDroneReceiver` 的 `Availability == Online`
- 影子机飞行速度按 JSON 中各路径节点的 `segment_speed` 字段插值，与实际无人机速度无关

---

### 十一、测试清单（无需真实无人机）【负责人：李昊泽】

以下功能均可在无真实无人机的情况下完成测试，要求在本次迭代结束后全部验收通过。

#### 11.1 主菜单关卡

| 测试项 | 验收标准 |
|--------|----------|
| 打开主菜单，UI 正常显示，无控制台报错 | 通过 |
| 输入槽位编号注册无人机 | 后端 `drones.json` 中出现对应记录，列表刷新 |
| 重复注册同一 slot | UI 显示冲突提示，不新增记录 |
| 删除已注册无人机 | 后端删除，列表刷新 |
| 后端未启动时打开主菜单 | UI 不崩溃，显示"后端连接失败"或等价提示 |
| 点击"队列编辑"按钮 | 正确切换到编辑关卡 |
| 点击"预演"按钮 | 正确切换到预演关卡 |

#### 11.2 编辑关卡

| 测试项 | 验收标准 |
|--------|----------|
| 进入编辑关卡，场景加载无报错 | 通过 |
| 添加航点、拖拽调整位置 | 航点正常创建和移动 |
| 点击保存，命名弹窗弹出 | 弹窗正常显示 |
| 输入合法文件名后确认保存 | `Saved/DronePaths/` 下出现对应 JSON 文件 |
| 输入非法字符（如 `\` `:` `*`）后尝试保存 | 提示非法字符，阻止保存 |
| 输入空文件名后尝试保存 | 确认按钮置灰或提示不允许为空 |
| 选择已保存文件进行播放 | 影子机重置到初始位置并开始飞行动画 |
| 点击停止 | 播放停止，影子机停在当前位置 |
| B 键返回主菜单 | 正确切换到主菜单 |

#### 11.3 预演关卡（无无人机）

| 测试项 | 验收标准 |
|--------|----------|
| 进入预演关卡，场景加载无报错 | 通过 |
| F 键切换自由视角与追随视角 | 追随视角返回上次选择的无人机（需求五修复后） |
| Shift 键显示/隐藏鼠标光标 | 按住 Shift 显示鼠标，松开隐藏 |
| B 键返回主菜单 | 正确切换 |
| R 键重置影子机（需求四） | 所有影子机移动到对应镜像机位置 |
| 打开阵列播放面板，列出 `Saved/DronePaths/` 中的文件 | 文件列表正常显示 |
| 勾选"自动分配"后点击"派发指令" | 发送 `auto_assign: true`，收到 `assignment_result` 后槽位显示分配结果 |
| 不勾选"自动分配"，手动指定槽位后派发 | 与当前行为一致 |
| 本地预演：影子机按路径播放动画 | 影子机飞行，镜像机不动 |

#### 11.4 通讯协议（使用 Mock 后端或 Apifox）

| 测试项 | 验收标准 |
|--------|----------|
| WebSocket 连接建立 | 连接成功，控制台无报错 |
| 后端推送 `power_on` 事件 | UE5 收到事件，镜像机锚点更新 |
| 后端推送 `lost_connection` 事件 | UI 状态面板显示"失联" |
| 后端推送 `low_battery alert`（value=15） | UI 显示低电量告警弹窗 |
| 后端推送 `assembling` 进度 | 集结弹窗进度条更新 |
| 后端推送 `assembly_complete` | 集结弹窗关闭或显示完成状态 |
| 后端推送 `assignment_result` | 阵列播放面板槽位映射更新 |
| UE5 发送 `move` 指令 | 后端收到并响应，Apifox 可观察到 WebSocket 消息 |
| UE5 发送 `pause` / `resume` 指令 | 后端收到并返回 `command_ack` |



预演模块 路径规划

点击切换无人机运行模式的按钮没有实装