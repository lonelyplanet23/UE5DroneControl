# UE5 无人机姿态同步 - 新手详细操作指南

## 🎯 目标
让 UE5 中的无人机模型显示真实无人机的姿态（俯仰、偏航、翻滚角度）

---

## 📋 第一步：编译 C++ 代码（必须）

### 1.1 打开 UE5 编辑器
1. 双击打开 `UE5DroneControl.uproject`
2. 等待 UE5 编辑器加载完成

### 1.2 编译 C++ 代码
由于我们修改了 C++ 代码，必须重新编译：

**方法A：在 UE5 编辑器中编译（推荐）**
```
1. 在 UE5 编辑器顶部菜单栏：
   Tools（工具） → Compile C++ Code（编译 C++ 代码）

2. 等待编译完成（可能需要 1-3 分钟）

3. 看到 "Compilation Successful!" 表示成功
```

**方法B：在 Visual Studio 中编译**
```
1. 右键点击 UE5DroneControl.uproject
2. 选择 "Generate Visual Studio project files"
3. 打开生成的 .sln 文件
4. 在 Visual Studio 中：
   Build（生成） → Build Solution（生成解决方案）
5. 编译成功后，返回 UE5 编辑器
```

### 1.3 重启 UE5 编辑器（建议）
编译完成后，关闭并重新打开 UE5 编辑器以确保使用最新代码。

---

## 📋 第二步：配置 RealTimeDroneReceiver Actor

### 2.1 找到你的 Drone Actor

在 UE5 编辑器中：

```
1. 打开 World Outliner（世界大纲）
   菜单栏 → Window（窗口） → World Outliner

2. 在 World Outliner 中搜索：
   - 输入 "Drone" 或 "RealTime"
   - 找到名为 "RealTimeDroneReceiver" 的 Actor

3. 单击选中这个 Actor
```

### 2.2 查看 Details 面板

选中 Actor 后，右侧会显示 **Details（细节）** 面板。

如果看不到 Details 面板：
```
菜单栏 → Window（窗口） → Details
```

### 2.3 配置姿态同步参数

在 Details 面板中，找到 **Real Time Config** 分类：

```
┌─────────────────────────────────────┐
│ Details（细节）                      │
├─────────────────────────────────────┤
│ Transform（变换）                    │
│ ...                                  │
│ ▼ Real Time Config                  │  ← 展开这个分类
│   ├─ Listen Port: 8888              │
│   ├─ Auto Detect Port: ☐            │
│   ├─ Smooth Speed: 5.0              │
│   ├─ Scale Factor: 1.0              │
│   ├─ Auto Face Target: ☐            │  ← 关闭自动朝向
│   └─ Use Received Rotation: ☑       │  ← 开启使用接收的旋转
└─────────────────────────────────────┘
```

### 2.4 关键配置说明

**重点配置（必须）：**

| 参数 | 设置值 | 说明 |
|------|--------|------|
| **Use Received Rotation** | ✅ **勾选** | 使用无人机发送的真实姿态 |
| **Auto Face Target** | ❌ **不勾选** | 关闭自动朝向移动方向 |

**其他参数（可选）：**

| 参数 | 推荐值 | 说明 |
|------|--------|------|
| Listen Port | 8888 | UDP 监听端口 |
| Smooth Speed | 5.0 | 位置平滑速度（越大越快） |
| Scale Factor | 1.0 | 坐标缩放因子 |

---

## 📋 第三步：运行和测试

### 3.1 启动数据桥接

在 Windows 命令行：
```bash
cd D:\RedAlert\UE5DroneControl
python drone_data_bridge.py
```

等待看到：
```
[OK] 已发送数据 #1: timestamp=...
[OK] 已发送数据 #2: timestamp=...
```

### 3.2 运行 UE5 场景

在 UE5 编辑器中：
```
1. 点击工具栏的 "Play（播放）" 按钮
   或按 Alt + P

2. UE5 会进入游戏模式
```

### 3.3 查看 Output Log

打开输出日志以查看调试信息：
```
菜单栏 → Window（窗口） → Developer Tools（开发工具）
       → Output Log（输出日志）
```

在 Output Log 中搜索关键字 `ProcessPacket`，应该看到：

```
LogTemp: Warning: >>> [ProcessPacket] 收到 3379 字节数据
LogTemp: Warning: >>> [ProcessPacket] 解析成功! NED Position=(0.00, 0.00, 0.00) [米]
LogTemp: Warning: >>> [ProcessPacket] UE5 偏移量=(0.00, 0.00, 0.00) [厘米]
LogTemp: Warning: >>> [ProcessPacket] 姿态: Pitch=0.00°, Yaw=0.00°, Roll=0.00°
LogTemp: Log: >>> [QuatToEuler] NED Quat=(0.000, 0.000, 0.000, 1.000)
LogTemp: Log: >>> [QuatToEuler] UE5 Euler=(Pitch:0.00, Yaw:0.00, Roll:0.00)
```

---

## 📋 第四步：验证姿态同步

### 4.1 让无人机旋转

如果你有真实无人机：
1. 解锁无人机
2. 手动旋转或倾斜无人机
3. 观察 UE5 中的模型是否跟随旋转

### 4.2 观察 Output Log

当无人机姿态变化时，应该看到：
```
>>> [ProcessPacket] 姿态: Pitch=15.30°, Yaw=45.20°, Roll=-5.10°
>>> [QuatToEuler] NED Quat=(0.123, 0.456, 0.789, 0.321)
>>> [QuatToEuler] UE5 Euler=(Pitch:15.30, Yaw:45.20, Roll:-5.10)
```

### 4.3 在视口中观察

在 UE5 游戏视口中：
- **Pitch（俯仰）**：机头上下点头
- **Yaw（偏航）**：左右转向
- **Roll（翻滚）**：左右倾斜

---

## 🔧 常见问题排查

### Q1: 无人机模型没有旋转

**检查清单：**

1. ✅ 确认 `Use Received Rotation = true`
   ```
   在 Details 面板检查这个选项是否勾选
   ```

2. ✅ 确认数据中有四元数
   ```
   在 Output Log 搜索 "QuatToEuler"
   应该看到非零的四元数值
   ```

3. ✅ 确认编译成功
   ```
   重新编译 C++ 代码并重启 UE5
   ```

### Q2: 姿态数据全是 0

**原因：** 无人机没有移动或旋转

**解决：**
```
1. 确保无人机已解锁
2. 手动旋转无人机
3. 检查 MicroXRCE Agent 是否正常运行
```

### Q3: 旋转方向不对

**原因：** 坐标系转换可能需要微调

**解决：**
```
在代码中调整四元数转换：
修改 QuatToEuler 函数中的 ConversionQuat
```

---

## 📊 参数调整建议

### 平滑旋转速度

如果旋转太慢或太快，调整 Tick 函数中的插值速度：

**位置：** `RealTimeDroneReceiver.cpp` 第 158 行

```cpp
// 当前值是 10.0f，可以调整：
FRotator NewRot = FMath::RInterpTo(CurrentRot, TargetRotation, DeltaTime, 10.0f);
                                                                            ↑
// 数值越大，旋转越快
// 建议范围：5.0 ~ 20.0
```

**修改步骤：**
1. 打开 Visual Studio
2. 找到这一行
3. 修改数值
4. 重新编译

---

## 🎨 可视化调试（可选）

### 在屏幕上显示姿态角度

代码中已经包含屏幕调试：

```cpp
// 在 ProcessPacket 函数中
if (GEngine)
{
    FString DebugMsg = FString::Printf(TEXT("Pos: %s | Rel: %s | Rot: %s"),
        *NewTarget.ToString(), *NEDOffset.ToString(), *NewRotation.ToString());
    GEngine->AddOnScreenDebugMessage(123, 0.1f, FColor::Green, DebugMsg);
}
```

这会在游戏视口左上角显示实时姿态信息。

---

## ✅ 验收标准

完成配置后，系统应该：

- [x] UE5 编译成功
- [x] Details 面板中 `Use Received Rotation = true`
- [x] Output Log 显示四元数和欧拉角
- [x] 无人机模型随真实无人机旋转
- [x] Pitch、Yaw、Roll 角度正确

---

## 📝 下一步

1. **测试真实飞行**：让无人机起飞并观察同步效果
2. **调整平滑参数**：根据实际效果微调速度
3. **多无人机扩展**：参考《多无人机部署指南.md》

---

生成时间：2026-01-25
作者：Claude Code Assistant
