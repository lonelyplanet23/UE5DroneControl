# 搭建UI原型 - 零基础图文教程

> 本文档适用于**完全没写过UE蓝图**的同学，照着做就行，每一步都有详细说明。
> 所有逻辑我都已经写在C++里了，你只需要拖UI + 绑定按键。

---

## 📋 准备工作（先做这个！）

### 1. 编译C++代码

**操作步骤：**
1. 关闭UE5编辑器
2. 右键点击 `UE5DroneControl.uproject` → `Generate Visual Studio project files`
3. 双击打开 `UE5DroneControl.sln`
4. 在Visual Studio中，右键 `UE5DroneControl` 项目 → `Build`
5. 等待编译完成，如果有报错，看本文档最后一节「编译问题修复」

---

## 🎯 第一部分：创建4个Widget蓝图（每个5分钟，共20分钟）

---

### ✅ 第1个：WBP_DroneListItem（无人机列表项）

**步骤1：新建Widget**
1. 在内容浏览器中，进入 `Content/DroneOps/UI/` 文件夹
2. 右键空白处 → `User Interface` → `Widget Blueprint`
3. 弹出的窗口中，**搜索框输入 `DroneListItemWidget`**
4. 选中 `DroneListItemWidget`（这是我写的C++基类）
5. 点击 `Select`
6. 命名为 `WBP_DroneListItem`

**步骤2：搭建UI布局**
打开 `WBP_DroneListItem`，在左侧 `Widget Tree` 面板操作：

1. 右键 `Canvas Panel` → `Replace` → 搜索 `Horizontal Box` → 选中
2. 选中 `Horizontal Box`，在右侧 `Details` 面板：
   - `Slot` → `Size` → `Height` 填 `40`
3. 右键 `Horizontal Box` → `Add Child` → 搜索 `Text` → 添加
   - 选中这个Text，在右侧 `Details` 面板最上方：
   - **把名字改成 `DroneNameText`**（必须完全一样！大小写也要对！）
   - `Content` → `Text` 填 "无人机01"（测试用，运行时会自动替换）
   - `Font` → `Size` 改成 `14`
4. 右键 `Horizontal Box` → `Add Child` → 搜索 `Spacer` → 添加
   - 选中Spacer，右侧 `Size` → `Width` 填 `100`
5. 右键 `Horizontal Box` → `Add Child` → 搜索 `Border` → 添加
   - 选中这个Border，**改名为 `StatusIndicator`**（必须完全一样！）
   - `Brush` → `Color` 选绿色
   - `Slot` → `Size` → `Width` 填 `16`，`Height` 填 `16`
6. 在 `Border` 里面再添加一个Text：
   - 右键 `StatusIndicator` → `Add Child` → `Text`
   - **改名为 `StatusText`**（必须完全一样！）
   - `Content` → `Text` 填 "在线"

**步骤3：保存编译**
- 点击左上角 `Compile` 按钮（蓝色的）
- 点击 `Save`
- **不用写任何蓝图逻辑！** C++已经写好了！

---

### ✅ 第2个：WBP_DroneList（无人机列表总面板）

**步骤1：新建Widget**
1. 右键 → `Widget Blueprint`
2. Parent Class 搜索 `DroneListWidget` → 选中
3. 命名为 `WBP_DroneList`

**步骤2：搭建UI布局**
打开 `WBP_DroneList`：

1. 右键 `Canvas Panel` → `Replace` → 搜索 `Size Box`
2. 选中 `Size Box`，右侧Details：
   - `Width Override` 打勾 → 填 `300`
   - `Height Override` 打勾 → 填 `500`
   - `Slot` → `Horizontal Alignment` 选 `Right Aligned`
   - `Slot` → `Vertical Alignment` 选 `Top Aligned`
3. 右键 `Size Box` → `Add Child` → 搜索 `Border`
   - 选中Border，`Brush` → `Color` 选淡灰色（比如 RGBA: 0.1, 0.1, 0.1, 0.8）
4. 右键 `Border` → `Add Child` → 搜索 `Vertical Box`
   - 选中Vertical Box，`Slot` → `Padding` 四个方向都填 `10`
5. 右键 `Vertical Box` → `Add Child` → 搜索 `Text`
   - `Content` → `Text` 填 "无人机列表"
   - `Font` → `Size` 改成 `16`，打勾 `Bold`
6. 右键 `Vertical Box` → `Add Child` → 搜索 `Spacer`
   - `Size` → `Height` 填 `10`
7. 右键 `Vertical Box` → `Add Child` → 搜索 `Scroll Box`
   - **改名为 `DroneScrollBox`**（必须完全一样！）

**步骤3：设置ListItemClass**
在 `WBP_DroneList` 的蓝图编辑器中：

1. 点击右上角的 `Graph` 标签（切换到蓝图模式，不用写代码，只是设置变量）
2. 在左侧 `Variables` 面板，找到 `UI` 分类下的 `ListItem Class`
3. 点击它，右侧 `Default Value` 下拉，选 `WBP_DroneListItem`

**步骤4：保存编译**
- 点击 `Compile` → 点击 `Save`
- **不用写任何蓝图逻辑！**

---

### ✅ 第3个：WBP_Toast（右下角消息弹窗）

**步骤1：新建Widget**
1. 右键 → `Widget Blueprint`
2. Parent Class 搜索 `ToastWidget` → 选中
3. 命名为 `WBP_Toast`

**步骤2：搭建UI布局**
打开 `WBP_Toast`：

1. 右键 `Canvas Panel` → `Replace` → 搜索 `Size Box`
2. 选中 `Size Box`，右侧Details：
   - `Width Override` 打勾 → 填 `250`
   - `Height Override` 打勾 → 填 `60`
   - `Slot` → `Horizontal Alignment` 选 `Right Aligned`
   - `Slot` → `Vertical Alignment` 选 `Bottom Aligned`
3. 右键 `Size Box` → `Add Child` → 搜索 `Border`
   - `Brush` → `Color` 选深灰色（RGBA: 0.05, 0.05, 0.05, 0.9）
   - `Brush Settings` → `Corner Radius` 四个方向都填 `8`（圆角）
4. 右键 `Border` → `Add Child` → 搜索 `Text`
   - **改名为 `MessageText`**（必须完全一样！）
   - `Content` → `Text` 填 "测试消息"
   - `Color and Opacity` 选白色
   - `Font` → `Size` 改成 `14`

**步骤3：保存编译**
- `Compile` → `Save`
- **不用写任何蓝图逻辑！**

---

### ✅ 第4个：WBP_AssemblyPopup（集结进度弹窗）

**步骤1：新建Widget**
1. 右键 → `Widget Blueprint`
2. Parent Class 搜索 `AssemblyPopupWidget` → 选中
3. 命名为 `WBP_AssemblyPopup`

**步骤2：搭建UI布局**
打开 `WBP_AssemblyPopup`：

1. 右键 `Canvas Panel` → `Replace` → 搜索 `Size Box`
2. 选中 `Size Box`，右侧Details：
   - `Width Override` 打勾 → 填 `350`
   - `Height Override` 打勾 → 填 `120`
   - `Slot` → `Horizontal Alignment` 选 `Center Aligned`
   - `Slot` → `Vertical Alignment` 选 `Center Aligned`
3. 右键 `Size Box` → `Add Child` → 搜索 `Border`
   - `Brush` → `Color` 选蓝色（RGBA: 0.0, 0.2, 0.5, 0.9）
   - `Corner Radius` 都填 `8`
4. 右键 `Border` → `Add Child` → 搜索 `Vertical Box`
   - `Padding` 四个方向都填 `20`
5. 右键 `Vertical Box` → `Add Child` → 搜索 `Text`
   - `Content` → `Text` 填 "集结中"
   - 颜色白色，字体 `18`，加粗
6. 右键 `Vertical Box` → `Add Child` → 搜索 `Spacer`
   - `Height` 填 `10`
7. 右键 `Vertical Box` → `Add Child` → 搜索 `Text`
   - **改名为 `ProgressText`**（必须完全一样！）
   - `Content` → `Text` 填 "已就位: 0/3"
   - 颜色白色，字体 `16`
8. 右键 `Vertical Box` → `Add Child` → 搜索 `Button`
   - 按钮里面的Text改成 "关闭"
   - 选中Button，在Details面板找到 `Events` → `On Clicked`
   - 点击 `On Clicked` 右边的 `+` 号，会自动跳转到蓝图
   - 在蓝图中，拖拽 `On Clicked` 的执行线，搜索并调用 `On Close Clicked` 函数（这个函数C++已经写好了！）
   - 就这一个节点，不需要其他任何东西！

**步骤3：保存编译**
- `Compile` → `Save`

---

## 🎯 第二部分：关卡蓝图绑定按键（10分钟）

### 步骤1：打开关卡蓝图

1. 在UE5编辑器主窗口，顶部菜单栏 → `Blueprints`
2. 点击 `Open Level Blueprint`

### 步骤2：添加按键绑定

在关卡蓝图中，右键空白处，搜索以下节点：

---

**绑定 L 键（显示/隐藏无人机列表）：**
1. 右键搜索 `Keyboard Event L` → 选中 `L`
2. 从 `Pressed` 引脚拖拽出来，搜索 `Show Drone List`
3. 就这么简单！只有2个节点连起来就行

---

**绑定 T 键（Toast测试）：**
1. 右键搜索 `Keyboard Event T` → 选中 `T`
2. 从 `Pressed` 拖拽出来，搜索 `Show Toast`
3. 选中 `Show Toast` 节点
4. 在 `Show Toast` 节点的 `Message` 输入框，填 `"测试: 无人机注册成功!"`（注意引号！）
5. `Duration` 保持默认 `2.0` 就行

---

**绑定 Y 键（集结演示）：**
1. 右键搜索 `Keyboard Event Y` → 选中 `Y`
2. 从 `Pressed` 拖拽出来，搜索 `Show Assembly Demo`
3. `Total Count` 填 `3`，`Step Interval` 填 `1.0`

---

### 步骤3：编译保存关卡蓝图

1. 点击左上角 `Compile` 按钮
2. 点击 `Save`
3. 关闭关卡蓝图窗口

---

## 🎯 第三部分：运行测试（5分钟）

1. 点击UE5编辑器主窗口的 `Play` 按钮（▶️）
2. 按以下键测试：

| 按键 | 预期效果 |
|------|----------|
| **L 键** | 屏幕右上角显示无人机列表，3架无人机（2绿1红） |
| **T 键** | 屏幕右下角弹出Toast消息 "测试: 无人机注册成功!"，2秒后自动消失 |
| **Y 键** | 屏幕中间弹出蓝色集结弹窗，数字自动从 0/3 → 1/3 → 2/3 → 3/3，然后自动关闭 |

3. 打开 `Window` → `Developer Tools` → `Output Log`，可以看到每隔5秒打印一次HTTP请求日志

---

## ❌ 编译问题修复

如果你编译遇到错误：

### 问题1："Cannot open include file: xxx.h"

**解决方法：**
把我写的这5个文件都删掉，重新从邮件下载或者让我重新发一遍。确保文件都放在 `Source/UE5DroneControl/UI/` 目录下。

### 问题2："UTextBlock not found" 或 "UBorder not found"

**解决方法：**
打开 `DroneListItemWidget.h`，在最上面添加：
```cpp
#include "Components/TextBlock.h"
#include "Components/Border.h"
```

### 问题3：其他编译错误

**把 Output Log 中的错误信息复制给我，我帮你修！**

---

## 🎉 完成！

恭喜你！2小时内就搞定了：
- ✅ 4个UI组件
- ✅ 完整的列表显示
- ✅ Toast自动消失弹窗
- ✅ 集结进度自动演示
- ✅ 一行蓝图逻辑都没写！（都是C++写好的）

后面要加功能只要在C++里加函数，蓝图直接调用就行！
