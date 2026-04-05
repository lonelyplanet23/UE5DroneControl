# FR-03 无人机信息面板调试总结

本文档记录了 FR-03 任务开发调试过程中遇到的所有问题和解决方案。

---

## 问题 1：按中键没反应，日志显示 `[FR-03] Middle click - no drone under cursor`

### 原因
碰撞设置问题：`ARealTimeDroneReceiver` 和 `AMultiDroneCharacter` 的胶囊体虽然设置了碰撞预设 `Pawn`，但 **`ECC_Visibility` 通道没有显式设置为 `Block`**。而 `GetHitResultUnderCursor(ECC_Visibility)` 需要这个通道阻塞才能检测到 Actor。

### 解决方案
在构造函数中添加碰撞通道设置：
```cpp
// 确保 Visibility 通道阻塞，鼠标检测才能命中无人机
Capsule->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
```

---

## 问题 2：运行时触发 `__debugbreak()` 断点，调用栈在 `drone selectablegen.cpp`

### 原因
对于声明为 `UFUNCTION(BlueprintNativeEvent)` 的接口函数，**不能直接调用 `GetDroneId()`**，必须使用 `IDroneSelectableInterface::Execute_GetDroneId()` 静态调用方式。

### 解决方案
修改 `ADroneOpsPlayerController::Tick()` 中的调用方式：
```cpp
// 错误写法（触发断言）：
if (IDroneSelectableInterface* Drone = Cast<IDroneSelectableInterface>(HoveredActor)) {
    HoveredDroneId = Drone->GetDroneId();
}

// 正确写法：
if (Cast<IDroneSelectableInterface>(HoveredActor) != nullptr) {
    HoveredDroneId = IDroneSelectableInterface::Execute_GetDroneId(HoveredActor);
}
```

---

## 问题 3：`GetHitResultUnderCursor` 返回 `nullptr`，什么都检测不到

### 原因
无人机静态网格的 `Trace Response → Visibility` 默认是 `Ignore`，即使胶囊体设置了 `Block`，但你点击的是mesh，光线会穿过mesh，什么都打不到。

### 解决方案
在编辑器中手动修改：
1. 选中无人机 → 找到 Static Mesh Component
2. Collision Presets → 改成 **Custom**
3. 在 Trace Responses 中，找到 **Visibility** → 勾选 **Block**

---

## 问题 4：C++ 正确广播了事件，但WBP_DroneOpsHUD没有任何反应

### 原因
HUD widget 根本没有被创建出来，所以 `Event Construct` 没执行，事件也没绑定。原来设计只说要在HUD里绑定事件，但没说HUD要谁创建。

### 解决方案
1. 在 `ADroneOpsPlayerController` 中添加：
   - `DroneOpsHUDWidgetClass` 和 `DroneInfoPanelWidgetClass` 两个可配置属性
   - 在 `BeginPlay()` 中自动创建 HUD 并添加到 Viewport
   - 使用 `ConstructorHelpers` 在C++构造函数中自动查找默认路径的widget

2. 如果你手动放关卡蓝图创建也可以，效果一样。

---

## 问题 5：面板弹出来了，但是全屏，不是设计的320宽度小面板

### 原因
根节点对齐设置不对，`Horizontal Alignment` 和 `Vertical Alignment` 默认是 `Fill`，会填满整个屏幕。

### 解决方案
在 `WBP_DroneInfoPanel` 编辑器中：
1. 选中根节点（Canvas Slot）
2. `Horizontal Alignment` → 改为 **Left**
3. `Vertical Alignment` → 改为 **Top**
4. 确认顶层已经有 `Size Box`，并且 `Width Override = 320`

---

## 问题 6：面板显示出来了，但是所有字段都是空的

### 原因
蓝图 `UpdateFromSnapshot` 函数中，各个 `Set 变量` 节点之间 **白色执行线没连上**，只有第一个节点执行了，后面都没执行。

### 解决方案
从上到下把所有 `Set` 节点用白色执行线连起来，每个分支也要连好执行线。确保所有赋值都执行到。

---

## 问题 7：更新时间显示负数

### 原因
`LastUpdateTime` 原来用 `FPlatformTime::Seconds()` 存系统绝对时间，蓝图中用 `Get World Time Seconds`（游戏相对时间）相减，结果肯定是负数。

### 解决方案
1. `ARealTimeDroneReceiver::PushTelemetry()` 中改为 `Snap.LastUpdateTime = GetWorld()->GetTimeSeconds();`
2. `UDroneTelemetryComponent::PushSnapshot()` 中分开存储：
   - `LastUpdateRealTime` → 保留 `FPlatformTime::Seconds()` 给C++内部间隔计算
   - `CurrentSnapshot.LastUpdateTime` → 使用 `GetWorld()->GetTimeSeconds()` 给UI显示

---

## 问题 8：点击第二、第三架无人机，显示的都是第一架的数据

### 原因
关卡中放置的三个无人机，**`DroneId` 属性都保持默认值 1**，没有分别改成 1/2/3。所以不管点哪一架，`GetDroneId` 返回都是 1，打开的都是ID=1的数据。

### 解决方案
在关卡中分别选中每个无人机：
- 第一架 → `DroneId = 1`
- 第二架 → `DroneId = 2`
- 第三架 → `DroneId = 3`

---

## 最终修复清单（代码修改汇总）

| 文件 | 修改内容 |
|------|----------|
| `RealTimeDroneReceiver.cpp` | 添加 `ECC_Visibility` → `ECR_Block` |
| `MultiDroneCharacter.cpp` | 添加头文件 `CapsuleComponent.h`，添加碰撞设置和 `Visibility` Block |
| `DroneOpsPlayerController.cpp` | 修复接口调用方式，添加C++直接创建信息面板逻辑，自动获取数据调用 `UpdateFromSnapshot` |
| `DroneOpsPlayerController.h` | 添加 widget class 成员变量和 `OpenDroneInfoPanel` 声明 |
| `DroneTelemetryComponent.cpp` | 修复 `LastUpdateTime` 时间戳类型，分离C++内部使用和UI使用的时间戳 |

---

## 验收确认

经过所有调试修复，现在满足设计文档所有验收标准：

✅ 只有悬停无人机+中键才弹出面板  
✅ 显示无人机ID/名称  
✅ 显示在线状态（带颜色：在线绿色/离线灰色/失联红色）  
✅ 显示当前位置 X/Y（单位：米）  
✅ 显示当前高度（单位：米）  
✅ 显示当前速度（单位：m/s）  
✅ 显示姿态 Yaw/Pitch/Roll（单位：度）  
✅ 显示最近更新时间（格式：xx.xs 前）  
✅ 点击关闭按钮正确关闭面板  
✅ 不悬停无人机按中键不弹出  
✅ 多无人机分别设置不同DroneId后，点击哪架显示哪架  
✅ 数据单位正确，格式正确
