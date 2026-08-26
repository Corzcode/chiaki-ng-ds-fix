
![chiaki-ng Logo](gui/res/chiaking-logo.svg)

# [chiaki-ng](https://streetpea.github.io/chiaki-ng/)

An open source PlayStation remote play project serving as the next-generation of Chiaki with improvements and ongoing support now that the original Chiaki project is in maintenance mode only. [Click here to see the accompanying site for documentation, updates and more](https://streetpea.github.io/chiaki-ng/).

**Upstream source / 上游来源:** [https://github.com/streetpea/chiaki-ng](https://github.com/streetpea/chiaki-ng)

## Features added in this fork / 本分支新增功能

### Gyro Steering / 陀螺仪转向

- Added gyro steering: map DS5 gyro roll to the left stick for driving games / 新增陀螺仪转向：将 DS5 陀螺仪偏转映射到左摇杆，适合驾驶类游戏
- Configurable sensitivity, deadzone, invert and one-touch rest point (recenter) / 可配置灵敏度、死区、反向转向与一键回正中心点
- Live angle / output stick preview in the settings dialog / 设置界面中实时预览转向角度与输出摇杆值
- Re-centers steering on config change and auto-applies settings at startup / 配置变更时自动回到当前握持中心，启动时自动应用配置

### DualSense Controller / DualSense 手柄

- Fixed native DualSense gyro/accel, restoring v1.8.0 accuracy / 修复原生 DualSense 陀螺仪/加速度计读数，恢复 v1.8.0 精度
- Fixed Bluetooth rumble too strong and rumble lost after hot-swap / 修复蓝牙连接下震动过强、热切换后震动消失
- Suppressed false continuous rumble on flat road in GT7 over Bluetooth / 抑制蓝牙下 GT7 平路持续误震动
- Auto-reopen wired haptics device if it fails to open or is not restored after hot-swap / 有线触觉设备打不开或热切换后未恢复时自动重开
- Haptics anti-latency: flush stale queue when backlog exceeds a configurable threshold / 触觉防延迟：队列积压超过可配置阈值时清空旧数据

### HDR Display / HDR 显示

- Added auto inverse tone-mapping option / 新增自动逆色调映射选项
- Added "Target Peak HDR Only" option based on client display HDR, not source / 新增 Target Peak HDR Only 选项，仅基于客户端显示端 HDR 而非源信号
- Fixed libplacebo HDR transfer detection using `PL_COLOR_TRC_*` / 修正 libplacebo HDR 传输曲线检测（使用 `PL_COLOR_TRC_*`）

### Video Quality & Stability / 画质与稳定性

- Added RAVU Lite r4 spatial upscaler option / 新增 RAVU Lite r4 空间缩放选项
- Paced the present loop to the video frame rate with backlog-aware catch-up, reducing GPU load and stutter / 显示循环与视频帧率同步并带背压感知追赶，降低 GPU 占用与卡顿
- Fixed use-after-free on session teardown and quick controller disconnect / 修复会话销毁与快速断开手柄时的使用后释放（UAF）
- Stops the render loop after the stream exits to avoid stuck GPU usage / 退出流后停止渲染循环，避免 GPU 占用卡住

### UI / 界面

- Added Simplified Chinese (zh_CN) UI with language selector / 新增简体中文界面与语言选择器
- Stream stats overlay mode toggle and controller battery display / 流统计信息叠加层新增模式切换与手柄电量显示
- Added a frame-process latency metric to the stats overlay / 统计叠加层新增帧处理延迟指标
- Hide the cursor automatically after 1s of inactivity in fullscreen / 全屏下空闲 1 秒后自动隐藏鼠标光标
- Fixed dropdowns not closing on outside click and aligned the controller settings tab / 修复下拉框未在外部点击时关闭、并对齐控制器设置页布局

## Discord
[chiaki-ng community Discord](https://discord.gg/tAMbRuwXDH)

## Disclaimer
This project is not endorsed or certified by Sony Interactive Entertainment LLC.

Chiaki is a Free and Open Source Software Client for PlayStation 4 and PlayStation 5 Remote Play
for Linux, FreeBSD, OpenBSD, Android, macOS, Windows, Nintendo Switch and potentially even more platforms.
