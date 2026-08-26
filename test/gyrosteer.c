// test/gyrosteer.c
// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <munit.h>

#include <chiaki/gyrosteer.h>

#include <math.h>

// 构造绕体轴 Z(前进轴)旋转 angle_deg 的四元数(模块内 BODY_RIGHT = 体轴 X)
static ChiakiOrientation roll_quat(float angle_deg)
{
	float half = angle_deg * (float)M_PI / 180.0f / 2.0f;
	ChiakiOrientation q;
	q.x = 0.0f;
	q.y = 0.0f;
	q.z = sinf(half);
	q.w = cosf(half);
	return q;
}

static void settle(ChiakiGyroSteer *gs, const ChiakiOrientation *orient)
{
	// 多次 update 让低通收敛
	for(int i = 0; i < 30; i++)
		chiaki_gyro_steer_update(gs, orient, 0.016f);
}

static MunitResult test_init_inactive(const MunitParameter params[], void *user)
{
	ChiakiGyroSteer gs;
	chiaki_gyro_steer_init(&gs, true, false, CHIAKI_GYRO_STEER_MODE_ANGLE, 3.0f, 30.0f, 0.f, 0.f, 0.f, 0.f, 1.f);
	munit_assert_false(chiaki_gyro_steer_is_active(&gs));
	munit_assert_float(chiaki_gyro_steer_get_left_x(&gs), ==, 0.0f);
	return MUNIT_OK;
}

static MunitResult test_zero_orientation(const MunitParameter params[], void *user)
{
	ChiakiGyroSteer gs;
	chiaki_gyro_steer_init(&gs, true, false, CHIAKI_GYRO_STEER_MODE_ANGLE, 3.0f, 30.0f, 0.f, 0.f, 0.f, 0.f, 1.f);
	ChiakiOrientation ident = { 0.0f, 0.0f, 0.0f, 1.0f };
	chiaki_gyro_steer_set_rest(&gs, &ident);
	munit_assert_true(chiaki_gyro_steer_is_active(&gs));
	settle(&gs, &ident);
	munit_assert_float(chiaki_gyro_steer_get_left_x(&gs), ==, 0.0f);
	munit_assert_float(chiaki_gyro_steer_get_angle_deg(&gs), ==, 0.0f);
	return MUNIT_OK;
}

static MunitResult test_full_deflection(const MunitParameter params[], void *user)
{
	ChiakiGyroSteer gs;
	chiaki_gyro_steer_init(&gs, true, false, CHIAKI_GYRO_STEER_MODE_ANGLE, 3.0f, 30.0f, 0.f, 0.f, 0.f, 0.f, 1.f);
	ChiakiOrientation ident = { 0.0f, 0.0f, 0.0f, 1.0f };
	chiaki_gyro_steer_set_rest(&gs, &ident);
	ChiakiOrientation roll = roll_quat(30.0f); // 满偏
	settle(&gs, &roll);
	// 低通 30 次迭代仍有收敛残差(~6.8e-5),用容差断言而非精确 ==
	munit_assert_double_equal(chiaki_gyro_steer_get_left_x(&gs), 1.0, 3);
	return MUNIT_OK;
}

static MunitResult test_negative_deflection(const MunitParameter params[], void *user)
{
	ChiakiGyroSteer gs;
	chiaki_gyro_steer_init(&gs, true, false, CHIAKI_GYRO_STEER_MODE_ANGLE, 3.0f, 30.0f, 0.f, 0.f, 0.f, 0.f, 1.f);
	ChiakiOrientation ident = { 0.0f, 0.0f, 0.0f, 1.0f };
	chiaki_gyro_steer_set_rest(&gs, &ident);
	ChiakiOrientation roll = roll_quat(-30.0f);
	settle(&gs, &roll);
	munit_assert_double_equal(chiaki_gyro_steer_get_left_x(&gs), -1.0, 3);
	return MUNIT_OK;
}

static MunitResult test_deadzone(const MunitParameter params[], void *user)
{
	ChiakiGyroSteer gs;
	chiaki_gyro_steer_init(&gs, true, false, CHIAKI_GYRO_STEER_MODE_ANGLE, 3.0f, 30.0f, 0.f, 0.f, 0.f, 0.f, 1.f);
	ChiakiOrientation ident = { 0.0f, 0.0f, 0.0f, 1.0f };
	chiaki_gyro_steer_set_rest(&gs, &ident);
	ChiakiOrientation roll = roll_quat(2.0f); // 死区内
	settle(&gs, &roll);
	munit_assert_float(chiaki_gyro_steer_get_left_x(&gs), ==, 0.0f);
	return MUNIT_OK;
}

static MunitResult test_invert(const MunitParameter params[], void *user)
{
	ChiakiGyroSteer gs;
	chiaki_gyro_steer_init(&gs, true, true, CHIAKI_GYRO_STEER_MODE_ANGLE, 3.0f, 30.0f, 0.f, 0.f, 0.f, 0.f, 1.f);
	ChiakiOrientation ident = { 0.0f, 0.0f, 0.0f, 1.0f };
	chiaki_gyro_steer_set_rest(&gs, &ident);
	ChiakiOrientation roll = roll_quat(30.0f);
	settle(&gs, &roll);
	munit_assert_double_equal(chiaki_gyro_steer_get_left_x(&gs), -1.0, 3);
	return MUNIT_OK;
}

static MunitResult test_set_rest(const MunitParameter params[], void *user)
{
	ChiakiGyroSteer gs;
	chiaki_gyro_steer_init(&gs, true, false, CHIAKI_GYRO_STEER_MODE_ANGLE, 3.0f, 30.0f, 0.f, 0.f, 0.f, 0.f, 1.f);
	ChiakiOrientation ident = { 0.0f, 0.0f, 0.0f, 1.0f };
	ChiakiOrientation tilted = roll_quat(20.0f);
	chiaki_gyro_steer_set_rest(&gs, &tilted); // 把 20° 处设为回正中心
	settle(&gs, &tilted);
	munit_assert_float(chiaki_gyro_steer_get_left_x(&gs), ==, 0.0f);
	settle(&gs, &ident); // 回到平放,相对 rest 反向 20°
	// 软死区平移法: (20-3)/(30-3) = 17/27
	munit_assert_double_equal(chiaki_gyro_steer_get_left_x(&gs), -(17.0f / 27.0f), 3);
	return MUNIT_OK;
}

// --- RATE_SPRING 模式测试 ---

static ChiakiGyroSteer rate_steer(float spring)
{
	ChiakiGyroSteer gs;
	chiaki_gyro_steer_init(&gs, true, false, CHIAKI_GYRO_STEER_MODE_RATE_SPRING,
		0.f, 30.f, 90.f, spring, 5.f, 0.35f, 1.5f);
	ChiakiOrientation ident = { 0.0f, 0.0f, 0.0f, 1.0f };
	chiaki_gyro_steer_set_rest(&gs, &ident);
	return gs;
}

// 缓慢持续转动应能累积出可感知输出(解决"缓慢移动无反应")
static MunitResult test_rate_accumulates_during_turn(const MunitParameter params[], void *user)
{
	ChiakiGyroSteer gs = rate_steer(2.0f);
	ChiakiOrientation still = roll_quat(0.0f);
	for(int i = 0; i < 30; i++)
		chiaki_gyro_steer_update(&gs, &still, 0.016f);
	munit_assert_float(chiaki_gyro_steer_get_left_x(&gs), ==, 0.0f);

	bool grew = false;
	float prev = 0.0f;
	for(int i = 0; i < 100; i++)
	{
		ChiakiOrientation o = roll_quat((i + 1) * 0.8f); // 每帧 +0.8° → 50 °/s,止于 80°(<90°)
		chiaki_gyro_steer_update(&gs, &o, 0.016f);
		float lx = chiaki_gyro_steer_get_left_x(&gs);
		if(lx > prev + 1e-4f)
			grew = true;
		prev = lx;
	}
	munit_assert_true(grew);
	munit_assert_true(chiaki_gyro_steer_get_left_x(&gs) > 0.2f); // 可持续输出显著转角
	return MUNIT_OK;
}

// 停止转动(静止)后弹簧应使其指数回中
static MunitResult test_rate_spring_returns_center(const MunitParameter params[], void *user)
{
	ChiakiGyroSteer gs = rate_steer(2.0f);
	for(int i = 0; i < 100; i++)
	{
		ChiakiOrientation o = roll_quat((i + 1) * 0.6f); // 渐变转到 60°,发出持续转向
		chiaki_gyro_steer_update(&gs, &o, 0.016f);
	}
	munit_assert_true(chiaki_gyro_steer_get_left_x(&gs) > 0.1f);
	ChiakiOrientation hold = roll_quat(60.0f);
	for(int i = 0; i < 600; i++)
		chiaki_gyro_steer_update(&gs, &hold, 0.016f); // 持握静止(idle 0.2×,回中较慢,需更久)
	munit_assert_double_equal(chiaki_gyro_steer_get_left_x(&gs), 0.0, 2);
	return MUNIT_OK;
}

// 低于角速度死区的抖动不应累积转向
static MunitResult test_rate_deadzone_blocks_jitter(const MunitParameter params[], void *user)
{
	ChiakiGyroSteer gs = rate_steer(2.0f);
	ChiakiOrientation still = roll_quat(0.0f);
	for(int i = 0; i < 30; i++)
		chiaki_gyro_steer_update(&gs, &still, 0.016f);
	for(int i = 0; i < 100; i++)
	{
		ChiakiOrientation o = roll_quat(i * 0.05f); // 每帧 +0.05° → 3.125 °/s < 死区5
		chiaki_gyro_steer_update(&gs, &o, 0.016f);
	}
	munit_assert_true(fabsf(chiaki_gyro_steer_get_left_x(&gs)) < 0.01f);
	return MUNIT_OK;
}

// 低速持续转动:活动回中极弱(0.04×),缓慢转动也能累积到显著输出(不被回中吞掉)
static MunitResult test_rate_slow_steady_state_positive(const MunitParameter params[], void *user)
{
	ChiakiGyroSteer gs = rate_steer(4.2f);
	// 20°/s 连续转动 3.84s(止于 72°<90°):活动回中极弱,输出应累积到显著值
	for(int i = 0; i < 240; i++)
	{
		ChiakiOrientation o = roll_quat(i * 0.3f);
		chiaki_gyro_steer_update(&gs, &o, 0.016f);
	}
	float lx = chiaki_gyro_steer_get_left_x(&gs);
	munit_assert_true(lx > 0.3f);
	return MUNIT_OK;
}

// 转动后短暂停顿(<0.35s)仍处弱弹簧,不应明显误回中(过弯短暂停顿不丢转向)
static MunitResult test_rate_short_pause_no_recenter(const MunitParameter params[], void *user)
{
	ChiakiGyroSteer gs = rate_steer(4.2f);
	for(int i = 0; i < 100; i++)
	{
		ChiakiOrientation o = roll_quat((i + 1) * 0.8f); // 50°/s 持续转动
		chiaki_gyro_steer_update(&gs, &o, 0.016f);
	}
	float before = chiaki_gyro_steer_get_left_x(&gs);
	munit_assert_true(before > 0.2f);
	// 短暂静止 0.16s(10帧) < idle_threshold 0.35s,弱弹簧 0.8 只造成少量衰减
	ChiakiOrientation hold = roll_quat(80.0f);
	for(int i = 0; i < 10; i++)
		chiaki_gyro_steer_update(&gs, &hold, 0.016f);
	float after = chiaki_gyro_steer_get_left_x(&gs);
	munit_assert_true(after > before * 0.8f);
	return MUNIT_OK;
}

// 完全静止超过 0.35s 后切强弹簧,应显著回中
static MunitResult test_rate_long_idle_recenters(const MunitParameter params[], void *user)
{
	ChiakiGyroSteer gs = rate_steer(4.2f);
	for(int i = 0; i < 100; i++)
	{
		ChiakiOrientation o = roll_quat((i + 1) * 0.8f);
		chiaki_gyro_steer_update(&gs, &o, 0.016f);
	}
	float before = chiaki_gyro_steer_get_left_x(&gs);
	munit_assert_true(before > 0.2f);
	// 完全静止 1.92s:idle 超 0.35s 后 0.2×(0.84)回中,需更久才能显著回中
	ChiakiOrientation hold = roll_quat(80.0f);
	for(int i = 0; i < 120; i++)
		chiaki_gyro_steer_update(&gs, &hold, 0.016f);
	float after = chiaki_gyro_steer_get_left_x(&gs);
	munit_assert_true(after < before * 0.4f);
	return MUNIT_OK;
}

MunitTest tests_gyrosteer[] = {
	{ "/init_inactive", test_init_inactive, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
	{ "/zero_orientation", test_zero_orientation, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
	{ "/full_deflection", test_full_deflection, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
	{ "/negative_deflection", test_negative_deflection, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
	{ "/deadzone", test_deadzone, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
	{ "/invert", test_invert, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
	{ "/set_rest", test_set_rest, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
	{ "/rate_accumulates_during_turn", test_rate_accumulates_during_turn, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
	{ "/rate_spring_returns_center", test_rate_spring_returns_center, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
	{ "/rate_deadzone_blocks_jitter", test_rate_deadzone_blocks_jitter, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
	{ "/rate_slow_steady_state_positive", test_rate_slow_steady_state_positive, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
	{ "/rate_short_pause_no_recenter", test_rate_short_pause_no_recenter, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
	{ "/rate_long_idle_recenters", test_rate_long_idle_recenters, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
	{ NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL }
};
