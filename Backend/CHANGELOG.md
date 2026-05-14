# 后端变更记录

> 项目：UE5DroneControl — 后端 C++ 服务  
> 日期：2026-05-14

---

## Change 1：后端目录整合

将 `BackEnd/` 和 `Backend/` 合并为唯一的 `Backend/` 目录（关皓元在 Windows 下开发时因大小写不敏感将二者视为同一目录）。`BackEnd/` 中有用文件（mock_server、integration_week3、vcpkg.json、验收文档）迁入 Backend/；旧 `BackEnd(before)/`（Python Demo 脚本和废弃 C++ 原型）作为存档保留；清除 Python `__pycache__/` 缓存。

## Change 2：执行引擎集成（从 Howie 分支）

从 Howie 分支拉取 `execution_engine.h/.cpp`（814 行），集成侦察/巡逻/攻击三种执行模式和实时避障。修改 `main.cpp` 创建 `ExecutionEngine` 并绑定 MoveCallback / TelemetryGetter / StateGetter 三个回调和遥测通知链；修改 `http_server.h/.cpp` 构造函数接收 `ExecutionEngine&` 并在集结完成时自动调用 `StartTasks()`，停止时调用 `StopAll()`；`CMakeLists.txt` 加入新源文件。

## Change 3：集结避障与路径规划

新增 `AssignmentSolver`（匈牙利算法，Kuhn-Munkres 实现，O(n³)）和 `AssemblyPlanner`（P1 最优分配 + P2 线段距离计算 + 高度分离）。`AssemblyController::Start()` 集成规划器：发起集结时，匈牙利算法将每架无人机分配到最近的集结点（总飞行距离全局最优），然后两两计算航线线段的最短三维距离，若两条航线接近到 4m（2×2m 安全柱）以内则判定冲突，通过图染色给冲突无人机分配不同飞行高度（zigzag 交替 ±1m）。配置新增 `assembly_safety_cylinder_m`（默认 2.0m）。新增 10 个单元测试覆盖匈牙利、线段距离、冲突检测和高度分离。

## Change 4：关皓元接口适配

`DroneManager` 构造函数新增 `low_battery_threshold` 参数和 `ProcessMoveCommandNed()` 方法（直接接受 NED 坐标，供执行引擎和集结规划器调用），电量告警使用可配置阈值并加入去重逻辑。`HeartbeatManager` 构造函数新增 `heartbeat_hz` 参数，心跳间隔改为 `1000/hz` ms 而非硬编码 500ms。`AssemblyController` 构造函数新增 `arrival_threshold_m`，新增 `MoveCommandCallback` 回调和 `GetConfig()` 方法，集结开始后自动通过回调向各机发送 NED 目标指令，到位判断阈值纳入配置。

## Change 5：代码审查 — 线程安全修复（12 项）

`DroneManager` 所有公开函数添加 `std::lock_guard<drones_mutex_>`（共 20 处），消除 `drones_` map 的多线程数据竞争。`UdpReceiver::running_` 改为 `std::atomic<bool>`，`callback_` 读写加锁。`UdpSender::targets_` 读写加锁。`HeartbeatManager::StopInternal` 加锁并改为锁外 join 避免死锁。`StateMachine::last_telemetry_time_` 改为 `std::atomic<int64_t>`（微秒），消除与 `CheckTimeout` 的数据竞争。`AssemblyController::state_` 和 `arrivals_` 操作加锁（5 处）。`HttpServer` 每连接线程从 `detach()` 改为存入 `conn_threads_` 向量，`Stop()` 中统一 join 消除 lifetime 问题。`jetson_bridge.py` 的 `_get_system_id()` 从硬编码 `return 2` 改为 `return self.slot + 1`（多机 system_id 纠正）；GPS fix 判断删除错误的赤道纬度检查，直接用 `self.global_pos.valid`。`SaveDrones()` 启用 `fstream::exceptions` 防止静默写入失败。`assembly_planner.cpp` 注释 BFS→DFS 修正。
