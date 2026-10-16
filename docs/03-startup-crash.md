# 实体键盘环境下的启动闪退（2026-09-29）

## 原因与证据

IMU 在输出 `UI ready` 前收到 `SIGSEGV`。目标板上旧版可执行文件的
GDB 调用栈为：

```text
focus_next_core
lv_group_remove_obj
hide_software_keyboards（递归遍历）
input_housekeeping
cm0_app_run
```

AppKit 的 `hide_software_keyboards()` 原先在遍历对象树时，无条件将
每个子对象移出焦点组。IMU 的普通按钮被移除后，焦点切换到可编辑的
灵敏度滑块，同步触发焦点回调并再次进入该遍历。嵌套调用清空焦点组，
导致外层 LVGL 焦点处理解引用空指针。

该问题可以使用 SDL dummy 驱动和模拟数据复现，不依赖 QMI8658 采集。
原有六项测试均能通过，因为渲染测试直接创建视图，没有执行完整的
`cm0_app_run()` 键盘焦点维护路径。

## 修复与回归测试

将独立 AppKit 仓库的修复同步到 IMU 的
`third_party/cm0-appkit/src/runtime.cpp`：只有实际的 `lv_keyboard`
对象退出焦点组，普通控件保持原有成员关系。LVGL 软件键盘由一个按钮
矩阵对象表示，按键不是需要额外移出焦点组的独立子对象。

AppKit 静态链接进应用，因此修改 SDK 源码后必须重新编译 IMU；只更新
设备上的 SDK 包不能修复已安装的可执行文件。

新增 `imu-runtime-portrait` 和 `imu-runtime-landscape`，通过 Python
标准库启动完整主机程序，使用 SDL dummy 驱动和模拟数据。测试要求
8 秒内出现 `UI ready`，继续运行至少 1 秒以覆盖多次输入维护及 IMU
更新，再发送 `SIGTERM`，要求 3 秒内以状态 0 退出。失败时输出运行日志，
并清理子进程。

## 验证结果

- 旧版主机程序：两项运行时测试均以 `SIGSEGV`（状态 `-11`）失败，
  未输出 `UI ready`。
- 修复后：IMU 全部 8 项 CTest 通过。
- 目标板：以 `pi`（UID 1000）运行修复程序，横竖屏各持续运行 8 秒。
  竖屏、横屏首帧分别为 171 ms、174 ms，收到 `SIGTERM` 后均以状态 0 退出。
- 目标进程打开 DRM、Goodix 触摸屏、TCA8418 键盘及 QMI8658 的六个
  IIO raw 通道；运行期间没有 IMU 错误日志。验证后恢复 launcher 服务。
- 修复包 `dist/lilygo-ui-imu_0.1.0_arm64.deb` 已安装到目标板。
  安装后的程序与已验证程序 SHA-256 一致：
  `1d5f7bb9d46428e3169cba310a316981c82c3e027a469e94b77ca3b303fb74f4`。
  旧程序保留在板上 `/tmp/lilygo-ui-imu-before-fix`，launcher 服务保持运行。

这些检查覆盖本次启动故障和短时间运行。主机测试使用模拟数据，不替代
实机采集、长期稳定性或所有控件的交互验证。
