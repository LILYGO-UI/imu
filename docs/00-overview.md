# LILYGO IMU 架构总览

本应用基于 LILYGO UI AppKit 和 LVGL 9.5，将 T-DisplayCM0 上 QMI8658 的加速度与
角速度转换为圆形区域中的运动球，同时显示六轴读数。目标设备运行 ARM64 Linux，
开发时可使用 SDL2 主机模拟器和内置模拟数据源。

项目介绍、依赖安装、构建和打包见 [中文 README](../README.zh-CN.md) 或
[English README](../README.md)。页面开发约束见 [UI 开发规范](01-user-interface.md)，
传感器通道、环境变量与校准算法见 [Sensor and Calibration](02-sensor-and-calibration.md)。

## 数据流

```text
QMI8658 / Linux IIO -> IioSource 工作线程 -> 加锁保存最新 SensorSnapshot
                                                        |
DemoSource 模拟数据 -------------------------------------+
                                                        v
LVGL 主线程定时器 -> ImuViewModel::tick -> 新鲜度检查 -> ImuModel
                                                         |
用户操作 -> ImuView 事件 -> ViewModel 命令 ----------------+
                                                         v
                   ImuView 刷新 <--- changed Subject <--- 状态更新
```

- `IioSource` 在独立线程中发现设备、读取六轴通道并转换单位。正常读取后等待
  `10 ms`，打开或读取失败后关闭通道，等待 `1000 ms` 再尝试连接。
- `SensorSnapshot` 保存读数、连接状态、时间戳、模拟标记和错误信息；互斥锁只保护
  最新快照和退出状态，设备读取在锁外执行。页面不直接访问 sysfs。
- View 的 LVGL 定时器每 `16 ms` 调用一次 ViewModel，传入实际经过时间。快照超过
  `250 ms` 时按断连处理；有效读数交给 Model，单次模型步长上限为 `50 ms`。
- Model 和 Subject 更新、LVGL 对象操作均在主线程执行。六轴文字最多约每
  `100 ms` 刷新一次，球的位置、大小和状态随状态通知更新。

这些间隔是调度目标，不代表传感器采样率或 UI 帧率保证。当前使用最新值快照，
没有历史数据队列或录制功能。

## 模块边界

| 模块 | 责任 |
| --- | --- |
| `src/main.cpp` | 进入 AppKit 运行时。 |
| `src/app.cpp` | 注册应用描述符，在运行时传入的根对象下装配和销毁页面。 |
| `src/pages/imu/imu_sensor.*` | `SensorSource` 接口、Linux IIO 读取、后台重连和模拟数据。 |
| `src/pages/imu/imu_model.*` | 重力方向估计、校准、灵敏度、坐标旋转、球位置和加速度脉冲。 |
| `src/pages/imu/imu_view_model.*` | 读取快照、检查超时、转发命令、生成状态文字并发布 Subject 通知。 |
| `src/pages/imu/imu_view.*` | LVGL 控件、圆形运动区域、响应式布局、事件转发和定时刷新。 |
| `src/components/` | 共享语义颜色、页面构建辅助函数和 Subject RAII 封装。 |

依赖方向为 `View -> ViewModel -> Model`。传感器与 Model 编译为
`lilygo_ui_imu_core`，只依赖 Box2D 和线程库，不依赖 LVGL；ViewModel、View 和应用
生命周期代码编译为 `lilygo_ui_imu_ui`，通过 `LilyGoUI::Runtime` 使用 LVGL。

目前只有一个 IMU 页面。页面私有类型保留在 `src/pages/imu/`，只有出现真实的
跨页面复用需求时才新增 `src/domain/` 或路由模块。

## 运动与操作

- 球的位置由相对校准中立方向的倾斜量决定，并经过平滑处理。保持固定姿态会停在
  对应位置，不会因持续倾斜而不断累积位移。灵敏度范围为 `25%` 至 `200%`。
- 陀螺仪参与重力方向预测，接近 `1 g` 的加速度用于修正方向；去除偏置后的 Z 轴
  角速度驱动球内白色标记旋转。
- 短时加速度会使球变大，随后恢复。最大半径为基础值的三倍，并进一步受可用圆形
  空间限制；球变大或视口缩小时，球及其标记仍应保持在边界内。
- 页面只提供灵敏度调节，不显示应用标题、暂停/继续、复位和校准按钮。
  加速度在左、角速度在右，各自纵向显示 X/Y/Z；竖屏读数位于全宽运动区域上方，
  横屏读数位于运动区域两侧，紧凑底栏显示连接状态和灵敏度；界面文案使用英文。
- Model 和 ViewModel 保留暂停、复位与校准命令，供内部调用与测试；页面不触发它们。
  暂停冻结球的显示但继续处理采样，复位恢复球的显示状态，校准用约一秒连续稳定
  读数记录中立方向和陀螺仪偏置。
- 显示旋转和 `LILYGO_IMU_ROTATION` 的附加角度用于球的平面坐标映射；六轴数值
  仍是传感器坐标系读数。校准及用户设置只保存在本次运行中。

项目固定使用 Box2D `2.4.1` 的向量和旋转数学类型。当前没有创建 `b2World`，
也没有刚体、碰撞或重力滚动模拟；不要将姿态映射描述为完整物理引擎仿真。

## 连接与失败策略

- Linux 默认读取真实 IIO 设备，其他主机默认使用模拟数据；可以显式选择数据源，
  具体优先级见 [传感器配置](02-sensor-and-calibration.md)。
- 设备不可用、通道读取失败或快照超时时，状态显示 `Offline`，运动区域显示
  `QMI8658: waiting for connection`，六轴读数显示 `--`。
- 断连停止运动更新并取消正在进行的校准，保留最后球状态、已有校准结果、灵敏度
  和内部暂停状态。IIO 后台持续尝试重连，恢复后继续更新；不会自动切换为模拟数据。
- 非有限数值或无效时间步长不进入模型计算。设备错误变化时写入标准错误日志，
  页面使用统一的等待连接提示；详细故障原因应查看日志。

## 生命周期

`app_open()` 先创建 ViewModel，再创建引用它的 View。页面在调用者传入的
`context->root` 下创建，不能假设自己拥有整个 display。

关闭时先销毁 View：删除 `16 ms` 定时器，再删除页面根对象，由 LVGL 一并释放
子对象和对象绑定的 observer，随后清空缓存指针。最后销毁 ViewModel 和传感器
数据源；`IioSource` 设置退出标志、唤醒等待并 `join()` 工作线程。此顺序保证
LVGL 对象不会引用已经释放的 Subject 或回调对象。

## 工程与发布约束

- 使用 C++17 和 CMake 3.24 或更新版本；AppKit 附带 LVGL 的解压配置需要
  `DOWNLOAD_EXTRACT_TIMESTAMP`。稳定构建入口为
  `host-simulator` 和 `cm0-cross`；LPM 的配置、启动、测试及打包命令见 README。
- 平台 SDK 通过 `find_package(LilyGoUI CONFIG REQUIRED)` 引入，应用不直接
  编译 AppKit，也不引用 Launcher 源码。`third_party/cm0-appkit` 和
  `third_party/box2d` 是锁定版本的子模块。
- `lpm.toml` 是产品元数据源，包名和可执行文件为 `lilygo-ui-imu`，应用标识为
  `cc.lilygo.ui.Imu`。CMake 据此生成身份头文件、Launcher manifest、desktop 和
  AppStream 元数据，`data/` 保留通用模板。
- 图标和预览图位于 `assets/`。字体使用 AppKit 共享运行时字体；安装规则包含
  字体许可和 Box2D 许可，预览图不安装进应用包。
- `.cache/`、`.lpm/`、`.venv/`、`build/`、`dist/` 属于本地状态或构建产物，
  不作为源码提交。LPM 的 `publish.json` 是临时派生数据，不手工维护。

## 验收

模型、传感器或 UI 实现变更应完成主机配置、构建和测试：

```sh
cmake --preset host-simulator
cmake --build --preset host-simulator --parallel
ctest --preset host-simulator
```

当前有十项 CTest：`imu-core`、`imu-view-model`，`portrait`、`landscape`、
`compact`、`short` 四项 `imu-render-*`，以及横竖屏启动 `imu-runtime-*` 两项、
横竖屏键盘 `imu-keyboard-runtime-*` 两项。键盘测试经完整 SDL 运行时验证方向键调节、
指针与键盘交接、焦点恢复和灵敏度限值。测试使用合成输入或临时 IIO 文件，不要求连接
真实传感器。具体覆盖范围和渲染产物见 [UI 验收](01-user-interface.md#验收)。

UI 修改必须检查 `568x1232`、`1232x568`、`320x480` 和 `320x240` 的实际渲染，
包含球放大时的边界、断连提示和控件可达性。主机测试不能代替设备验证；修改 SDK、
工具链、安装或打包时还应完成 `cm0-cross` 构建、Debian 打包和设备运行检查。
