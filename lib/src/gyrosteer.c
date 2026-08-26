// lib/src/gyrosteer.c
// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <chiaki/gyrosteer.h>

#include <math.h>

// 握方向盘时"侧向"轴(体轴右侧)。真机校准:若倾斜方向反了改 {0,0,1} 或取负。
static const float BODY_RIGHT[3] = { 1.0f, 0.0f, 0.0f };

#define GYRO_STEER_CURVE 1.0f    // 响应曲线指数(1.0 线性)
#define GYRO_STEER_ALPHA 20.0f   // 输出平滑:alpha = 1 - exp(-dt*ALPHA),~50ms 时间常数
// 以下常量对齐 gyro-steer-algorithm.md(DS4Windows Ryochan7 方向盘模式)规格:
#define GYRO_STEER_LOWPASS_ALPHA 7.7f   // 角速度一阶低通,等效文档 lowpass_alpha=0.12@60Hz
// 回中强度(相对 spring_strength 的比例):
// 活动状态(转动或短暂停顿)回中很弱 = 0.04 倍;完全静止超 idle 阈值后 = 0.2 倍
#define GYRO_STEER_ACTIVE_SPRING_RATIO 0.04f
#define GYRO_STEER_IDLE_SPRING_RATIO 0.2f
#define GYRO_STEER_IDLE_RECENTER_DELAY_DEFAULT 0.35f // idle 回中延迟默认值(秒)
#define GYRO_STEER_OUTPUT_DEADZONE 0.02f // 输出死区:低于此的摇杆输出置零

static void quat_rotate_vec(const ChiakiOrientation *q, const float v[3], float out[3])
{
	float qx = q->x, qy = q->y, qz = q->z, qw = q->w;
	float tx = 2.0f * (qy * v[2] - qz * v[1]);
	float ty = 2.0f * (qz * v[0] - qx * v[2]);
	float tz = 2.0f * (qx * v[1] - qy * v[0]);
	out[0] = v[0] + qw * tx + (qy * tz - qz * ty);
	out[1] = v[1] + qw * ty + (qz * tx - qx * tz);
	out[2] = v[2] + qw * tz + (qx * ty - qy * tx);
}

static float wrap_180(float deg)
{
	while(deg > 180.0f)
		deg -= 360.0f;
	while(deg < -180.0f)
		deg += 360.0f;
	return deg;
}

static float roll_deg(const ChiakiOrientation *orient)
{
	float r[3];
	quat_rotate_vec(orient, BODY_RIGHT, r);
	// 侧轴相对水平面的倾角,即"握方向盘"时的左右倾斜角
	return atan2f(r[1], hypotf(r[0], r[2])) * 180.0f / (float)M_PI;
}

CHIAKI_EXPORT void chiaki_gyro_steer_init(ChiakiGyroSteer *gs,
		bool enabled, bool invert, ChiakiGyroSteerMode mode,
		float deadzone_deg, float max_angle_deg,
		float rate_max_deg_s, float spring_strength, float rate_deadzone_deg_s,
		float idle_delay, float curve_power)
{
	gs->enabled = enabled;
	gs->invert = invert;
	gs->mode = mode;
	gs->deadzone_deg = deadzone_deg;
	gs->max_angle_deg = max_angle_deg;
	gs->rate_max_deg_s = rate_max_deg_s;
	gs->spring_strength = spring_strength;
	gs->rate_deadzone_deg_s = rate_deadzone_deg_s;
	gs->idle_delay = idle_delay > 0.0f ? idle_delay : GYRO_STEER_IDLE_RECENTER_DELAY_DEFAULT;
	gs->curve_power = curve_power > 0.0f ? curve_power : 1.0f;
	gs->rest_valid = false;
	gs->rest_roll_deg = 0.0f;
	gs->angle_deg = 0.0f;
	gs->steering = 0.0f;
	gs->filtered_rate = 0.0f;
	gs->idle_time = 0.0f;
	gs->prev_roll_deg = 0.0f;
	gs->have_prev = false;
	gs->smoothed_left_x = 0.0f;
}

CHIAKI_EXPORT void chiaki_gyro_steer_set_rest(ChiakiGyroSteer *gs, const ChiakiOrientation *orient)
{
	gs->rest_roll_deg = roll_deg(orient);
	gs->rest_valid = true;
	// 角速度模式下一键回中:清空积分与静止计时,并以当前姿态为基准求下一帧角速度
	gs->steering = 0.0f;
	gs->filtered_rate = 0.0f;
	gs->idle_time = 0.0f;
	gs->prev_roll_deg = roll_deg(orient);
	gs->have_prev = true;
}

// 相邻两帧间 roll_deg(竖直倾角)变化,即转向角速度源。与 ANGLE 模式完全同轴(用户已验证有效)。
// 局限:roll_deg 超过 ±90° 会折叠,帧间差分跨边界时方向翻转;正常转向(<90°)无碍。
static float roll_deg_delta(const ChiakiOrientation *cur, float prev_roll_deg)
{
	return wrap_180(roll_deg(cur) - prev_roll_deg);
}

static float gyro_steer_angle_mapping(ChiakiGyroSteer *gs, float angle_deg)
{
	// 软死区(平移法):死区内为 0;越出阈值后减去死区作有效行程,
	// 边界处由 0 平滑起量,无硬截断跳变。PadForge/DS4Windows 成熟做法。
	float out = 0.0f;
	float mag = fabsf(angle_deg);
	if(mag > gs->deadzone_deg)
	{
		float slide = mag - gs->deadzone_deg;
		float range = fmaxf(gs->max_angle_deg - gs->deadzone_deg, 1e-4f);
		float norm = slide / range;
		if(norm > 1.0f)
			norm = 1.0f;
		out = copysignf(powf(norm, GYRO_STEER_CURVE), angle_deg);
	}
	return out;
}

CHIAKI_EXPORT void chiaki_gyro_steer_update(ChiakiGyroSteer *gs, const ChiakiOrientation *orient, float dt_sec)
{
	float roll_raw = roll_deg(orient);
	float angle_deg = wrap_180(roll_raw - gs->rest_roll_deg);
	gs->angle_deg = angle_deg;

	float out = 0.0f;
	if(gs->rest_valid)
	{
		switch(gs->mode)
		{
		case CHIAKI_GYRO_STEER_MODE_ANGLE:
			out = gyro_steer_angle_mapping(gs, angle_deg);
			break;
		case CHIAKI_GYRO_STEER_MODE_RATE_SPRING:
			{
				// 角速度增量:帧间 roll_deg(竖直倾角)差分 / dt(°/s),与 ANGLE 同轴
				float rate = 0.0f;
				if(gs->have_prev)
					rate = dt_sec > 1e-4f ? roll_deg_delta(orient, gs->prev_roll_deg) / dt_sec : 0.0f;
				gs->prev_roll_deg = roll_deg(orient);
				gs->have_prev = true;
				// 1. 一阶低通滤波降噪
				float lp = dt_sec > 0.0f ? 1.0f - expf(-dt_sec * GYRO_STEER_LOWPASS_ALPHA) : 1.0f;
				gs->filtered_rate += lp * (rate - gs->filtered_rate);
				// 2. 动态弹簧两档:活动状态回中极弱(0.04×);完全静止超 idle 阈值后 0.2×
				float angular_speed = fabsf(gs->filtered_rate);
				float spring = gs->spring_strength * GYRO_STEER_ACTIVE_SPRING_RATIO;
				if(angular_speed > gs->rate_deadzone_deg_s)
					gs->idle_time = 0.0f;
				else
				{
					gs->idle_time += dt_sec;
					if(gs->idle_time >= gs->idle_delay)
						spring = gs->spring_strength * GYRO_STEER_IDLE_SPRING_RATIO;
				}
				// 3. 角速度增量累加(速度式,无角度积分漂移)
				float drive = 0.0f;
				if(angular_speed > gs->rate_deadzone_deg_s && gs->rate_max_deg_s > 1e-4f)
					drive = gs->filtered_rate / gs->rate_max_deg_s;
				float s = gs->steering + drive * dt_sec;
				if(s > 1.0f)
					s = 1.0f;
				else if(s < -1.0f)
					s = -1.0f;
				// 4. 弹簧衰减回中(动态可变强度)
				s -= s * spring * dt_sec;
				gs->steering = s;
				// 5. 后处理(仅作用于输出,不清内部积分):非线性曲线 + 输出死区
				float out_val = copysignf(powf(fabsf(s), gs->curve_power), s);
				if(fabsf(out_val) < GYRO_STEER_OUTPUT_DEADZONE)
					out_val = 0.0f;
				out = out_val;
			}
			break;
		}
	}
	if(gs->invert)
		out = -out;
	float alpha = dt_sec > 0.0f ? 1.0f - expf(-dt_sec * GYRO_STEER_ALPHA) : 1.0f;
	gs->smoothed_left_x += alpha * (out - gs->smoothed_left_x);
}

CHIAKI_EXPORT float chiaki_gyro_steer_get_angle_deg(const ChiakiGyroSteer *gs)
{
	return gs->angle_deg;
}

CHIAKI_EXPORT float chiaki_gyro_steer_get_left_x(const ChiakiGyroSteer *gs)
{
	return gs->smoothed_left_x;
}

CHIAKI_EXPORT bool chiaki_gyro_steer_is_active(const ChiakiGyroSteer *gs)
{
	return gs->enabled && gs->rest_valid;
}
