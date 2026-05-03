# UE5 编译故障排查指南

本文档针对本项目（UE5DroneControl）在 Windows 上使用 Visual Studio 2022 编译时遇到的常见错误，提供原因分析和解决步骤。

---

## 错误一：C1076 / C3859 — 内部堆限制 / PCH 虚拟内存不足

### 错误信息

```
c1xx : error C3859: 未能创建 PCH 的虚拟内存
c1xx: note: 系统返回代码 1455: 页面文件太小，无法完成操作。
c1xx : fatal error C1076: 编译器限制: 达到内部堆限制
Error executing cl.exe (tool returned code: 2)
```

### 原因

UE5 使用预编译头（PCH，Precompiled Header）来加速编译。PCH 文件需要在内存中分配一块**连续的虚拟地址空间**，当以下情况同时发生时就会触发此错误：

- 多个 `cl.exe` 进程**并行运行**，每个都在尝试分配 PCH 内存
- 系统物理内存 + 页面文件的可用虚拟内存不足以满足所有并发请求

关键点：**Visual Studio 有自己的并行编译控制，独立于 UBT 的 `MaxParallelActions` 设置**。即使 `BuildConfiguration.xml` 里写了 `MaxParallelActions=1`，VS 仍可能同时启动多个编译任务，导致该配置失效。

### 解决步骤

#### 第一步：关闭 Visual Studio 的并行编译

在 Visual Studio 中：

**Tools → Options → Projects and Solutions → Build and Run**

将 `maximum number of parallel project builds` 改为 **1**。

> 这是最关键的一步。VS 的并行项目构建与 UBT 的并行控制是两套独立机制，必须在 VS 层面也限制。

#### 第二步：检查 UBT 配置文件

确认以下文件存在且内容正确：

**路径：** `C:\Users\<你的用户名>\AppData\Roaming\Unreal Engine\UnrealBuildTool\BuildConfiguration.xml`

```xml
<?xml version="1.0" encoding="utf-8" ?>
<Configuration xmlns="https://www.unrealengine.com/BuildConfiguration">
  <BuildConfiguration>
    <MaxParallelActions>1</MaxParallelActions>
    <bUseUnityBuild>false</bUseUnityBuild>
    <bUseAdaptiveUnityBuild>false</bUseAdaptiveUnityBuild>
  </BuildConfiguration>
  <WindowsPlatform>
    <PCHMemoryAllocationFactor>100</PCHMemoryAllocationFactor>
  </WindowsPlatform>
</Configuration>
```

各参数说明：

| 参数 | 说明 |
|---|---|
| `MaxParallelActions=1` | UBT 层面串行编译，避免多个 cl.exe 同时占用内存 |
| `bUseUnityBuild=false` | 关闭 Unity Build（UBT 默认将多个 .cpp 合并为一个大文件编译，单次内存峰值更高） |
| `bUseAdaptiveUnityBuild=false` | 关闭自适应 Unity Build |
| `PCHMemoryAllocationFactor=100` | PCH 内存分配因子，保持默认值 100；不要设置过高（如 400）会加剧内存压力 |

#### 第三步（可选）：用命令行编译验证

如果从 VS 编译仍有问题，可以绕过 VS 直接调用 UBT，确保配置生效：

```bat
"C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\Build.bat" ^
  UE5DroneControlEditor Win64 Development ^
  "D:\RedAlert\UE5DroneControl\UE5DroneControl.uproject"
```

命令行方式下 `MaxParallelActions=1` 一定生效，可以用来排除 VS 干扰。
