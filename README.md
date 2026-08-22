
![chiaki-ng Logo](gui/res/chiaking-logo.svg)

# [chiaki-ng](https://streetpea.github.io/chiaki-ng/)

An open source PlayStation remote play project serving as the next-generation of Chiaki with improvements and ongoing support now that the original Chiaki project is in maintenance mode only. [Click here to see the accompanying site for documentation, updates and more](https://streetpea.github.io/chiaki-ng/).

**Upstream source / 上游来源:** [https://github.com/streetpea/chiaki-ng](https://github.com/streetpea/chiaki-ng)

## Features added in this fork / 本分支新增功能

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

### UI / 界面

- Added Simplified Chinese (zh_CN) UI with language selector / 新增简体中文界面与语言选择器
- Stream stats overlay mode toggle and controller battery display / 流统计信息叠加层新增模式切换与手柄电量显示

## Discord
[chiaki-ng community Discord](https://discord.gg/tAMbRuwXDH)

## Disclaimer
This project is not endorsed or certified by Sony Interactive Entertainment LLC.

Chiaki is a Free and Open Source Software Client for PlayStation 4 and PlayStation 5 Remote Play
for Linux, FreeBSD, OpenBSD, Android, macOS, Windows, Nintendo Switch and potentially even more platforms.
