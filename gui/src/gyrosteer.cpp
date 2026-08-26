// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <gyrosteer.h>

#include <settings.h>

#include <cstdio>

// 临时诊断:打印 RATE 模式每帧中间量,用于定位真机无输出问题
static FILE *g_gyro_dbg = nullptr;
static int g_gyro_cnt = 0;

GyroSteerBridge::GyroSteerBridge(QObject *parent)
	: QObject(parent)
{
	chiaki_gyro_steer_init(&steer, false, false, CHIAKI_GYRO_STEER_MODE_RATE_SPRING,
		3.0f, 30.0f, 90.0f, 4.2f, 3.0f, 0.35f, 1.5f);
	elapsed.start();
}

void GyroSteerBridge::UpdateFromOrientation(const ChiakiOrientation &orient)
{
	last_orient = orient;
	have_orient = true;
	if(!steer.enabled)
		return;
	if(rest_pending)
	{
		chiaki_gyro_steer_set_rest(&steer, &orient);
		rest_pending = false;
	}
	// 纳秒精度计时:传感器事件高频触发时,毫秒精度会被截断为 0,导致角速度恒为 0
	qint64 now_ns = elapsed.nsecsElapsed();
	float dt = last_ns >= 0 ? (float)(now_ns - last_ns) / 1000000000.0f : 0.016f;
	last_ns = now_ns;
	if(dt < 0.0005f) // 防止过于密集的重复事件把单帧角速度算得离谱
		dt = 0.0005f;
	chiaki_gyro_steer_update(&steer, &orient, dt);
	if(!g_gyro_dbg)
		g_gyro_dbg = fopen("C:\\Users\\Corz\\gyro_debug.log", "w");
	if(g_gyro_dbg && (++g_gyro_cnt % 30) == 0)
		fprintf(g_gyro_dbg, "dt=%.5f ang=%.1f filt=%.2f steer=%.3f lx=%.3f\n",
			dt, steer.angle_deg, steer.filtered_rate, steer.steering, steer.smoothed_left_x);
	emit StateChanged();
}

void GyroSteerBridge::ApplyConfig(const Settings *settings)
{
	chiaki_gyro_steer_init(&steer,
		settings->GetGyroSteeringEnabled(),
		settings->GetGyroSteeringInvert(),
		(ChiakiGyroSteerMode)settings->GetGyroSteeringMode(),
		settings->GetGyroSteeringDeadzone(),
		settings->GetGyroSteeringSensitivity(),
		settings->GetGyroSteeringRateMax(),
		settings->GetGyroSteeringSpring(),
		settings->GetGyroSteeringRateDeadzone(),
		settings->GetGyroSteeringIdleDelay(),
		settings->GetGyroSteeringCurve());
	// 任何配置变更后都以当前握持为新回正中心
	if(steer.enabled)
		rest_pending = true;
	emit StateChanged();
}

void GyroSteerBridge::SetRestPoint()
{
	if(!have_orient)
		return;
	chiaki_gyro_steer_set_rest(&steer, &last_orient);
	rest_pending = false;
	chiaki_gyro_steer_update(&steer, &last_orient, 0.016f);
	emit StateChanged();
}

void GyroSteerBridge::RequestRecenter()
{
	if(have_orient)
	{
		chiaki_gyro_steer_set_rest(&steer, &last_orient);
		rest_pending = false;
	}
	else
		rest_pending = true; // 等下一个传感器事件校准
}
