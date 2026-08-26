// lib/include/chiaki/gyrosteer.h
// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_GYROSTEER_H
#define CHIAKI_GYROSTEER_H

#include "common.h"
#include "orientation.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 转向映射模式。
 * ANGLE       :绝对倾斜——手柄倾角相对回正角直接映射为左摇杆 X(松手停在当前角度)。
 * RATE_SPRING :角速度增量 + 动态弹簧回中——对 roll 角速度积分叠加,松开/静止时弹簧指数回中。
 */
typedef enum chiaki_gyro_steer_mode
{
	CHIAKI_GYRO_STEER_MODE_ANGLE = 0,
	CHIAKI_GYRO_STEER_MODE_RATE_SPRING = 1
} ChiakiGyroSteerMode;

/**
 * 把 DS5/DS4 姿态四元数的 roll(左右倾斜)映射为左摇杆 X。
 * 数值核心,无 SDL/QML 依赖,供 munit 单测。
 */
typedef struct chiaki_gyro_steer_t
{
	bool enabled;
	bool invert;
	ChiakiGyroSteerMode mode;

	// --- ANGLE 模式参数 ---
	float deadzone_deg;    // 死区半宽(°)
	float max_angle_deg;   // 满偏转角(°)

	// --- RATE_SPRING 模式参数 ---
	float rate_max_deg_s;      // 满偏角速度(°/s):达到该转速时输出增量归一化到 1
	float spring_strength;     // 闲置后强回中弹簧(1/s),文档 idle_spring
	float rate_deadzone_deg_s; // 运动判定阈值(°/s),文档 motion_threshold:高于它判定为转动手柄
	float idle_delay;          // 完全静止超过此秒数后切强弹簧回中(文档 idle_threshold,默认0.35)
	float curve_power;         // 输出非线性曲线指数(1.0 线性,1.4~1.8 适合赛车,文档 curve_power)

	float rest_roll_deg;   // 回正基准角(内部,ANGLE 模式校准用)
	bool rest_valid;
	float angle_deg;       // 当前识别转角(供预览)

	// 内部状态
	float steering;         // RATE_SPRING 积分输出 [-1,1]
	float filtered_rate;    // 低通滤波后的角速度(°/s)
	float idle_time;        // 连续静止时长(秒),用于"短暂停顿不误回中"
	float prev_roll_deg;    // 上一帧 roll_deg(°),用于求角速度(与 ANGLE 同轴)
	bool have_prev;
	float smoothed_left_x;
} ChiakiGyroSteer;

CHIAKI_EXPORT void chiaki_gyro_steer_init(ChiakiGyroSteer *gs,
		bool enabled, bool invert, ChiakiGyroSteerMode mode,
		float deadzone_deg, float max_angle_deg,
		float rate_max_deg_s, float spring_strength, float rate_deadzone_deg_s,
		float idle_delay, float curve_power);
CHIAKI_EXPORT void chiaki_gyro_steer_set_rest(ChiakiGyroSteer *gs, const ChiakiOrientation *orient);
CHIAKI_EXPORT void chiaki_gyro_steer_update(ChiakiGyroSteer *gs, const ChiakiOrientation *orient, float dt_sec);
CHIAKI_EXPORT float chiaki_gyro_steer_get_angle_deg(const ChiakiGyroSteer *gs);
CHIAKI_EXPORT float chiaki_gyro_steer_get_left_x(const ChiakiGyroSteer *gs);
CHIAKI_EXPORT bool chiaki_gyro_steer_is_active(const ChiakiGyroSteer *gs);

#ifdef __cplusplus
}
#endif

#endif // CHIAKI_GYROSTEER_H
