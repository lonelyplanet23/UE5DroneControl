# UE5DroneControl — 系统时序图

> 版本：v0.3 | 生成日期：2026-06-05
> 覆盖范围：系统初始化、注册、遥测、控制、集结、预演、整体数据流

---

## 1. 系统初始化

```mermaid
sequenceDiagram
    participant GI as GameInstance
    participant NM as DroneNetworkManager
    participant WS as DroneWebSocketClient
    participant HTTP as DroneHttpClient
    participant REG as DroneRegistrySubsystem

    GI->>NM: Initialize()
    NM->>HTTP: NewObject, BaseUrl=:8080
    NM->>WS: NewObject, ServerUrl=:8081/ws
    NM->>WS: OnMessage.AddDynamic(OnWsMessage)
    NM->>NM: StartPolling() [每3s一次]
    NM->>WS: Connect()
    WS-->>NM: OnConnected()
    NM->>HTTP: GET /api/drones
    HTTP-->>NM: OnDroneListResponse(json)
    NM->>REG: RegisterDrone(FDroneDescriptor)
    NM->>REG: MarkDroneAvailability(Online/Offline/Lost)
    REG-->>REG: OnDroneRegistered.Broadcast(DroneId)
```

---

## 2. 主菜单无人机注册

```mermaid
sequenceDiagram
    participant UI as MainMenuWidget(BP)
    participant MW as UMainMenuWidget(C++)
    participant NM as DroneNetworkManager
    participant HTTP as DroneHttpClient
    participant REG as DroneRegistrySubsystem

    UI->>MW: RegisterDrone(Slot, IpAddress)
    MW->>NM: RegisterDroneToBackend(Slot, IP, Callback)
    NM->>HTTP: POST /api/drones {name,slot,ip,port}
    HTTP-->>NM: OnRegisterDroneResponse(bSuccess, Body)
    alt 注册成功
        NM->>REG: RegisterDrone(FDroneDescriptor)
        NM->>REG: MarkDroneAvailability(Offline)
        NM->>HTTP: GET /api/drones [立即刷新]
        NM-->>MW: HandleBackendRegisterResponse(true)
        MW-->>UI: OnDroneRegistered(DroneId) [BP事件]
    else 失败
        NM-->>MW: HandleBackendRegisterResponse(false)
    end
```

---

## 3. 遥测下行（WebSocket → UE5 Actor）

```mermaid
sequenceDiagram
    participant PX4 as PX4无人机
    participant BE as 后端(8081/ws)
    participant WS as DroneWebSocketClient
    participant NM as DroneNetworkManager
    participant REG as DroneRegistrySubsystem
    participant RX as RealTimeDroneReceiver

    PX4->>BE: UDP YAML 遥测 10Hz
    BE->>WS: WS推送 {type:telemetry, drone_id, x,y,z, yaw,pitch,roll, speed, battery}
    WS-->>NM: OnWsMessage(json)
    NM->>NM: 解析 FDroneTelemetrySnapshot
    NM->>REG: UpdateTelemetry(DroneId, Snapshot)
    REG-->>REG: TelemetryCache[DroneId] = Snapshot
    REG-->>RX: OnTelemetryUpdated.Broadcast(DroneId, Snapshot)
    RX->>RX: OnWebSocketTelemetry() [检查DroneId匹配]
    RX->>RX: TargetLocation = AnchorWorldLocation + (x,y,z)
    RX->>RX: TargetRotation = (pitch, yaw, roll)
    Note over RX: Tick()插值平滑到TargetLocation/TargetRotation
```

---

## 4. power_on 事件 & GPS 锚点建立

```mermaid
sequenceDiagram
    participant BE as 后端(8081/ws)
    participant NM as DroneNetworkManager
    participant RX as RealTimeDroneReceiver

    BE->>NM: WS推送 {type:event, event:power_on, drone_id, gps_lat, gps_lon, gps_alt}
    NM->>NM: CachedGpsAnchors[DroneId] = {lat,lon,alt}
    NM-->>RX: OnDroneWsEvent.Broadcast(DroneId, "power_on", lat, lon, alt)
    RX->>RX: OnDroneWsEvent() [检查DroneId匹配]
    Note over RX: 调用Cesium将GPS→UE世界坐标
    RX->>RX: AnchorWorldLocation = UE世界坐标
    RX->>RX: bHasGpsAnchor = true
```

---

## 5. 控制指令上行（鼠标点击 → 移动）

```mermaid
sequenceDiagram
    participant User as 用户
    participant PC as DroneOpsPlayerController
    participant REG as DroneRegistrySubsystem
    participant RX as RealTimeDroneReceiver
    participant NM as DroneNetworkManager
    participant WS as DroneWebSocketClient
    participant BE as 后端

    User->>PC: 左键点击地图
    PC->>PC: OnPrimaryClick()
    PC->>PC: GetSelectableDroneUnderCursor() → null(地图点)
    PC->>PC: HandleMapClick(WorldLocation)
    PC->>REG: IsControlLocked(SelectedDroneId) → false
    PC->>PC: SetClickTargetLocation(WorldLocation, 1)
    PC->>PC: SendTargetCommand(DroneId, WorldLocation)
    PC->>REG: GetDroneCommandMode(DroneId) → Move
    PC->>REG: GetReceiverActor(DroneId) → RX
    PC->>RX: RX.bHasGpsAnchor? → true
    PC->>PC: SendLocation = WorldLocation - AnchorWorldLocation
    PC->>NM: SendMoveCommand(DroneId, SendLocation, Move)
    NM->>WS: SendMessage({mode:"move", drone_id:"N", x, y, z})
    WS->>BE: WebSocket发送
    BE->>BE: 转发UDP 24字节控制包→PX4
```

---

## 6. 多机选择与派发

```mermaid
sequenceDiagram
    participant User as 用户
    participant PC as DroneOpsPlayerController
    participant REG as DroneRegistrySubsystem
    participant NM as DroneNetworkManager

    User->>PC: Shift+左键点击无人机A
    PC->>REG: GetMultiSelectedDrones() → [A]
    PC->>REG: SetMultiSelectedDrones([A, B])
    PC->>REG: SetPrimarySelectedDrone(B)

    User->>PC: 左键点击地图目标点
    PC->>PC: HandleMapClick(WorldLocation)
    PC->>REG: GetMultiSelectedDrones() → [A, B]
    loop 每架多选无人机
        PC->>PC: SlotLocation = WorldLocation + ComputeMultiDispatchOffset(i, 100cm)
        PC->>NM: SendMoveCommand(DroneId_i, offset_i, mode)
    end
```

---

## 7. 阵列任务 & 集结流程

```mermaid
sequenceDiagram
    participant UI as AssemblyPopupWidget
    participant NM as DroneNetworkManager
    participant HTTP as DroneHttpClient
    participant BE as 后端
    participant WS as DroneWebSocketClient

    UI->>NM: SendArrayTaskFromData(PathDataMap, Scout, Callback)
    NM->>HTTP: POST /api/arrays {mode, paths:[{drone_id,waypoints}]}
    HTTP-->>NM: 200 OK {array_id: "a1"}
    NM-->>UI: OnComplete(true, body)
    UI->>UI: 显示集结进度弹窗

    loop 集结中
        BE->>WS: {type:assembling, array_id:"a1", ready_count:N, total_count:M}
        WS-->>NM: OnWsMessage()
        NM-->>UI: OnAssemblingProgress.Broadcast("a1", N, M)
        UI->>UI: UpdateProgress(N, M)
    end

    alt 集结完成
        BE->>WS: {type:assembly_complete, array_id:"a1"}
        WS-->>NM: OnWsMessage()
        NM-->>UI: OnAssemblyComplete.Broadcast("a1")
        UI->>UI: StartAutoDemo()
    else 超时
        BE->>WS: {type:assembly_timeout, array_id:"a1", ready_count, total_count}
        NM-->>UI: OnAssemblyTimeout.Broadcast("a1", N, M)
        UI->>UI: 显示超时提示
    end
```

---

## 8. 暂停 / 恢复

```mermaid
sequenceDiagram
    participant User as 用户
    participant PC as DroneOpsPlayerController
    participant NM as DroneNetworkManager
    participant WS as DroneWebSocketClient
    participant REG as DroneRegistrySubsystem
    participant RX as RealTimeDroneReceiver
    participant BE as 后端

    User->>PC: 按P键
    PC->>PC: OnPauseToggle()
    PC->>REG: GetMultiSelectedDrones() → [DroneId...]
    PC->>NM: SendPauseCommand([DroneIds], bPause)
    NM->>WS: SendMessage({type:"pause", drone_ids:[...]})
    WS->>BE: WebSocket发送
    loop 每架无人机
        PC->>REG: GetReceiverActor(Id) → RX
        PC->>RX: SetPaused(bPause)
    end
```

---

## 9. 关卡加载 & Drone Spawn（GameMode 启动流程）

```mermaid
sequenceDiagram
    participant UE as UE5引擎
    participant GM as DroneOpsGameMode
    participant NM as DroneNetworkManager
    participant CEL as CesiumGeoreference
    participant REG as DroneRegistrySubsystem
    participant RX as RealTimeDroneReceiver
    participant MD as MultiDroneCharacter

    UE->>GM: PreInitializeComponents()
    GM->>GM: 加载 BP_DroneOpsPlayerController

    UE->>GM: BeginPlay()
    GM->>GM: 加载 BP_RealTimeDrone / BP_MultiDroneCharacter
    GM->>GM: InitializeCoordinateService()
    GM->>REG: SetCoordinateService(CesiumCoordinateService)

    GM->>NM: HasPendingGeoreferenceOrigin()?
    alt 有待应用的GPS原点（从主菜单传入）
        GM->>CEL: SetOriginLatitude/Longitude/Height(lat,lon,alt)
        GM->>GM: bPendingSpawnAfterGeoreferenceUpdate = true
        CEL-->>GM: OnGeoreferenceUpdated()
        GM->>GM: SpawnReceiversFromRegistry()
    else 无 PendingOrigin（直接启动）
        GM->>GM: SpawnReceiversFromRegistry()
    end

    GM->>REG: GetAllDroneDescriptors()
    loop 每架已注册无人机
        GM->>RX: SpawnActorDeferred<ARealTimeDroneReceiver>
        GM->>RX: ApplyDescriptor(Desc, Availability)
        GM->>RX: FinishSpawning()
        GM->>MD: SpawnActorDeferred<AMultiDroneCharacter>
        GM->>MD: 设置 DroneId/Name/BitIndex 等
        GM->>MD: FinishSpawning()
    end

    GM->>GM: RetryPossessPlacedPawns() [定时重试]
    GM->>GM: PlayerController.Possess(MultiDroneCharacter)
```

---

## 10. 主菜单跳转预演关卡（含 Cesium 原点传递）

```mermaid
sequenceDiagram
    participant User as 用户
    participant MW as MainMenuWidget
    participant NM as DroneNetworkManager
    participant UE as UE5引擎
    participant GM as DroneOpsGameMode

    User->>MW: 填写经纬度 + 点"预演"按钮
    MW->>MW: OnGoToPreviewClicked()
    MW->>NM: IsBackendConnected()? → true
    MW->>NM: PendingOriginLatitude = lat
    MW->>NM: PendingOriginLongitude = lon
    MW->>NM: PendingOriginAltitude = alt
    MW->>UE: OpenLevel("DroneOpsLevel")

    Note over NM: 跨关卡数据驻留 GameInstance

    UE->>GM: BeginPlay() [新关卡]
    GM->>NM: HasPendingGeoreferenceOrigin() → true
    GM->>NM: ClearPendingGeoreferenceOrigin()
    GM->>GM: ApplyPendingGeoreferenceOrigin()
    GM->>GM: 延迟一帧后设置 CesiumGeoreference 原点
```

---

## 11. 本地预演回放（DronePlaybackManager）

```mermaid
sequenceDiagram
    participant UI as 预演关卡UI
    participant PM as DronePlaybackManager
    participant PA as DronePathActor
    participant DA as DroneActor(预演用)

    UI->>PM: PlayFromData(FDronePathsSaveData)
    PM->>PM: StopPlayback() [清理旧状态]
    loop 每条路径数据
        PM->>PA: SpawnActor<ADronePathActor>()
        PM->>PA: SetPathNumericId / bClosedLoop
        PM->>PA: AddWaypoint(location, speed) × N
        PM->>PA: RefreshPath()
        PM->>DA: SpawnPlaybackDrone(waypoint[0])
        PM->>PA: StartMovement(DroneActor)
        PM->>PM: ActivePaths.Add(PA)
        PM->>PM: ActiveDrones.Add(DA)
    end
    PM->>PM: bIsPlaying = true

    loop Tick()每帧
        PM->>PA: GetCurrentPathStatus()
        alt 全部完成 + bLoop
            PM->>PM: PlayFromData(CachedData) [循环]
        else 全部完成
            PM->>PM: StopPlayback()
        end
    end

    opt 用户暂停
        UI->>PM: PausePlayback()
        loop 每个ActivePath
            PM->>PA: 记录 SegmentIndex + RemainingTime
            PM->>PA: StopMovement()
        end
        PM->>PM: bIsPaused = true
    end

    opt 用户恢复
        UI->>PM: ResumePlayback()
        loop 每个PausedState
            PM->>PA: ResumeMovement(DroneActor, segIdx, remainTime)
        end
        PM->>PM: bIsPaused = false
    end
```

---

## 12. 整体数据流总览

```mermaid
sequenceDiagram
    participant PX4 as PX4无人机
    participant BE as 后端(8080/8081)
    participant NM as DroneNetworkManager
    participant REG as DroneRegistrySubsystem
    participant RX as RealTimeDroneReceiver
    participant PC as PlayerController
    participant UI as HUD/Widget

    Note over PX4,BE: 下行遥测链路（10Hz）
    PX4->>BE: UDP YAML
    BE->>NM: WS telemetry {x,y,z,yaw,pitch,roll}
    NM->>REG: UpdateTelemetry(DroneId, Snapshot)
    REG-->>RX: OnTelemetryUpdated → 更新Actor位置姿态
    REG-->>UI: OnTelemetryUpdated → 刷新HUD数据

    Note over PC,BE: 上行控制链路
    PC->>NM: SendMoveCommand(DroneId, offset, mode)
    NM->>BE: WS {mode,drone_id,x,y,z}
    BE->>PX4: UDP 24字节控制包

    Note over NM,REG: HTTP轮询（3s）
    NM->>BE: GET /api/drones
    BE-->>NM: [{id,slot,ip,status}...]
    NM->>REG: RegisterDrone / MarkAvailability

    Note over BE,UI: 事件/告警推送
    BE->>NM: WS event(power_on/lost) / alert(low_battery)
    NM-->>RX: OnDroneWsEvent → 建立GPS锚点
    NM-->>UI: OnDroneWsAlert → 显示告警
```
