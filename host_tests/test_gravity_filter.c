#include <stdlib.h>

#include "app_config.h"
#include "gravity_filter.h"
#include "unity.h"

void setUp(void) { gravity_reset(); }
void tearDown(void) {}

static imu_sample_t sample(int32_t ax, int32_t ay, int32_t az)
{
    imu_sample_t s = {.a = {ax, ay, az}, .w = {0, 0, 0}, .saturated = false};
    return s;
}

/* ================== Derleyici varsayımı ================== */

static void test_arithmetic_shift_of_negative(void)
{
    volatile int32_t v = -5; /* volatile: derleyici sonucu önceden hesaplamasın */
    TEST_ASSERT_EQUAL_INT32(-3, v >> 1);
    v = -64000;
    TEST_ASSERT_EQUAL_INT32(-1000, v >> 6);
}

/* ================== Sabit giriş ================== */

/* Yol haritası: sabit girişte a_lin ≈ 0 (burada tam 0) */
static void test_constant_input_gives_zero_linear_accel(void)
{
    imu_sample_t s = sample(0, 0, 1000);
    motion_t m;
    for (int i = 0; i < 1000; i++) {
        gravity_update(&s, false, &m);
        TEST_ASSERT_EQUAL_INT32(0, m.a_lin[AX_X]);
        TEST_ASSERT_EQUAL_INT32(0, m.a_lin[AX_Y]);
        TEST_ASSERT_EQUAL_INT32(0, m.a_lin[AX_Z]);
        TEST_ASSERT_EQUAL_INT32(0, m.a_lin_mag);
    }
}

/* İlk örnek tahmini doğrudan başlatır: eğik dururken açılınca geçiş yok */
static void test_first_sample_seeds_estimate(void)
{
    int32_t g[3];
    TEST_ASSERT_FALSE(gravity_get(g));

    imu_sample_t s = sample(500, -300, 800);
    motion_t m;
    gravity_update(&s, false, &m);

    TEST_ASSERT_TRUE(gravity_get(g));
    TEST_ASSERT_EQUAL_INT32(500, g[AX_X]);
    TEST_ASSERT_EQUAL_INT32(-300, g[AX_Y]);
    TEST_ASSERT_EQUAL_INT32(800, g[AX_Z]);
    TEST_ASSERT_EQUAL_INT32(0, m.a_lin_mag);
}

/* Küçük gürültü ortalamaya karışır, a_lin gürültü seviyesinde kalır */
static void test_noisy_still_hand_stays_small(void)
{
    srand(1234);
    motion_t m;
    for (int i = 0; i < 2000; i++) {
        imu_sample_t s = sample(rand() % 41 - 20, rand() % 41 - 20, 1000 + rand() % 41 - 20);
        gravity_update(&s, false, &m);
        TEST_ASSERT_LESS_OR_EQUAL_INT32(60, m.a_lin_mag); /* gürültü ±20 mg/eksen */
    }
    int32_t g[3];
    gravity_get(g);
    TEST_ASSERT_INT32_WITHIN(10, 1000, g[AX_Z]);
}

/* ================== Zaman sabiti ================== */

/* Yol haritası: adım girişte yaklaşık 60 örnekte (0.3 s) %63'e yaklaşma */
static void test_step_response_time_constant(void)
{
    motion_t m;
    imu_sample_t before = sample(0, 0, 1000);
    imu_sample_t after = sample(0, 0, 2000); /* +1000 mg adım */
    gravity_update(&before, false, &m);

    int32_t g[3];
    int n63 = -1;
    for (int n = 1; n <= 500; n++) {
        gravity_update(&after, false, &m);
        gravity_get(g);
        if (n63 < 0 && g[AX_Z] - 1000 >= 632) {
            n63 = n;
        }
    }
    /* Kuram: 1 - (63/64)^n = 0.632  ->  n ≈ 64 */
    TEST_ASSERT_INT_WITHIN(6, 64, n63);
    /* 500 örnek (2.5 s) sonra tam oturmuş */
    TEST_ASSERT_EQUAL_INT32(2000, g[AX_Z]);
}

/* Kararlı durumda kalıcı hata yok: iki yönde ve negatif değerlerde */
static void test_converges_exactly_both_directions(void)
{
    const int32_t targets[] = {1000, -1000, 7, -7, 0, 3999, -3999};
    motion_t m;
    int32_t g[3];
    for (unsigned t = 0; t < sizeof targets / sizeof targets[0]; t++) {
        imu_sample_t s = sample(targets[t], -targets[t], 0);
        for (int i = 0; i < 2000; i++) {
            gravity_update(&s, false, &m);
        }
        gravity_get(g);
        TEST_ASSERT_EQUAL_INT32(targets[t], g[AX_X]);
        TEST_ASSERT_EQUAL_INT32(-targets[t], g[AX_Y]);
        TEST_ASSERT_EQUAL_INT32(0, m.a_lin[AX_X]);
    }
}

/* ================== Dondurma ================== */

/* Yol haritası: donukken tahmin hiç değişmemeli */
static void test_freeze_request_keeps_estimate_exactly(void)
{
    motion_t m;
    imu_sample_t still = sample(0, 0, 1000);
    for (int i = 0; i < 100; i++) {
        gravity_update(&still, false, &m);
    }

    imu_sample_t push = sample(2500, 0, 1000); /* sert itme */
    for (int i = 0; i < 12; i++) {              /* 60 ms pencere */
        gravity_update(&push, true, &m);
        TEST_ASSERT_TRUE(m.gravity_frozen);
        TEST_ASSERT_EQUAL_INT32(2500, m.a_lin[AX_X]); /* hareket tamamen görünüyor */
        TEST_ASSERT_EQUAL_INT32(0, m.a_lin[AX_Z]);
    }
    int32_t g[3];
    gravity_get(g);
    TEST_ASSERT_EQUAL_INT32(0, g[AX_X]);
    TEST_ASSERT_EQUAL_INT32(1000, g[AX_Z]);
}

/* Karşılaştırma: dondurma olmasaydı pencere boyunca tahmin hareketi yutardı */
static void test_without_freeze_estimate_absorbs_motion(void)
{
    motion_t m;
    imu_sample_t still = sample(0, 0, 1000);
    imu_sample_t push = sample(2500, 0, 1000);
    for (int i = 0; i < 100; i++) {
        gravity_update(&still, false, &m);
    }
    for (int i = 0; i < 12; i++) {
        gravity_update(&push, false, &m);
    }
    TEST_ASSERT_FALSE(m.gravity_frozen);
    TEST_ASSERT_LESS_THAN_INT32(2500, m.a_lin[AX_X]); /* 12. örnekte artık daha küçük */
}

/* Yol haritası: jiroskop toplamı 150 °/s'yi aşınca otomatik donma */
static void test_fast_rotation_freezes_automatically(void)
{
    motion_t m;
    imu_sample_t s = sample(0, 0, 1000);
    gravity_update(&s, false, &m);

    /* tam sınırda: 1500 -> donmaz (sınır "aşma") */
    s = sample(300, 0, 900);
    s.w[AX_Y] = 1500;
    gravity_update(&s, false, &m);
    TEST_ASSERT_FALSE(m.gravity_frozen);

    /* eksenlerin mutlak toplamı: |-800| + |800| = 1600 -> donar */
    gravity_reset();
    s = sample(0, 0, 1000);
    gravity_update(&s, false, &m);
    s = sample(300, 0, 900);
    s.w[AX_X] = -800;
    s.w[AX_Z] = 800;
    gravity_update(&s, false, &m);
    TEST_ASSERT_TRUE(m.gravity_frozen);

    int32_t g[3];
    gravity_get(g);
    TEST_ASSERT_EQUAL_INT32(0, g[AX_X]);
    TEST_ASSERT_EQUAL_INT32(1000, g[AX_Z]);
}

static void test_saturated_sample_does_not_update(void)
{
    motion_t m;
    imu_sample_t s = sample(0, 0, 1000);
    gravity_update(&s, false, &m);

    s = sample(7999, 0, 1000);
    s.saturated = true;
    gravity_update(&s, false, &m);
    TEST_ASSERT_TRUE(m.gravity_frozen);

    int32_t g[3];
    gravity_get(g);
    TEST_ASSERT_EQUAL_INT32(0, g[AX_X]);
}

/* Dondurma kalkınca tahmin kaldığı yerden yeniden takip eder */
static void test_update_resumes_after_freeze(void)
{
    motion_t m;
    imu_sample_t a = sample(0, 0, 1000);
    imu_sample_t b = sample(0, 0, 500);
    gravity_update(&a, false, &m);
    for (int i = 0; i < 50; i++) {
        gravity_update(&b, true, &m);
    }
    for (int i = 0; i < 1000; i++) {
        gravity_update(&b, false, &m);
    }
    TEST_ASSERT_EQUAL_INT32(0, m.a_lin[AX_Z]);
}

/* ================== Diğer ================== */

static void test_gyro_passed_through(void)
{
    imu_sample_t s = sample(0, 0, 1000);
    s.w[AX_X] = 12;
    s.w[AX_Y] = -34;
    s.w[AX_Z] = 56;
    motion_t m;
    gravity_update(&s, false, &m);
    TEST_ASSERT_EQUAL_INT32(12, m.w[AX_X]);
    TEST_ASSERT_EQUAL_INT32(-34, m.w[AX_Y]);
    TEST_ASSERT_EQUAL_INT32(56, m.w[AX_Z]);
}

static void test_linear_accel_magnitude(void)
{
    motion_t m;
    imu_sample_t s = sample(0, 0, 1000);
    gravity_update(&s, false, &m);
    s = sample(300, 400, 1000); /* a_lin = (300, 400, 0) */
    gravity_update(&s, true, &m);
    TEST_ASSERT_EQUAL_INT32(500, m.a_lin_mag);

    /* en kötü durum: taşma olmamalı */
    gravity_reset();
    s = sample(-8000, -8000, -8000);
    gravity_update(&s, false, &m);
    s = sample(8000, 8000, 8000); /* a_lin = 16 000 her eksende */
    gravity_update(&s, true, &m);
    TEST_ASSERT_INT32_WITHIN(1, 27713, m.a_lin_mag); /* 16000 * sqrt(3) */
}

static void test_isqrt(void)
{
    TEST_ASSERT_EQUAL_UINT32(0, isqrt_u32(0));
    TEST_ASSERT_EQUAL_UINT32(1, isqrt_u32(1));
    TEST_ASSERT_EQUAL_UINT32(1, isqrt_u32(3));
    TEST_ASSERT_EQUAL_UINT32(2, isqrt_u32(4));
    TEST_ASSERT_EQUAL_UINT32(3, isqrt_u32(15));
    TEST_ASSERT_EQUAL_UINT32(4, isqrt_u32(16));
    TEST_ASSERT_EQUAL_UINT32(1000, isqrt_u32(1000000));
    TEST_ASSERT_EQUAL_UINT32(65535, isqrt_u32(0xFFFFFFFFu));
    for (uint32_t r = 0; r <= 65535; r += 13) { /* 65535^2 uint32'e sığan en büyük kare */
        uint32_t sq = r * r;
        TEST_ASSERT_EQUAL_UINT32(r, isqrt_u32(sq));
        if (sq > 0) {
            TEST_ASSERT_EQUAL_UINT32(r - 1, isqrt_u32(sq - 1));
        }
    }
}

static void test_reset_forgets_estimate(void)
{
    motion_t m;
    imu_sample_t s = sample(0, 0, 1000);
    gravity_update(&s, false, &m);
    gravity_reset();

    int32_t g[3];
    TEST_ASSERT_FALSE(gravity_get(g));
    s = sample(0, 1000, 0);
    gravity_update(&s, false, &m);
    TEST_ASSERT_EQUAL_INT32(0, m.a_lin_mag); /* yeniden başlatıldı, eski tahmin yok */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_arithmetic_shift_of_negative);
    RUN_TEST(test_constant_input_gives_zero_linear_accel);
    RUN_TEST(test_first_sample_seeds_estimate);
    RUN_TEST(test_noisy_still_hand_stays_small);
    RUN_TEST(test_step_response_time_constant);
    RUN_TEST(test_converges_exactly_both_directions);
    RUN_TEST(test_freeze_request_keeps_estimate_exactly);
    RUN_TEST(test_without_freeze_estimate_absorbs_motion);
    RUN_TEST(test_fast_rotation_freezes_automatically);
    RUN_TEST(test_saturated_sample_does_not_update);
    RUN_TEST(test_update_resumes_after_freeze);
    RUN_TEST(test_gyro_passed_through);
    RUN_TEST(test_linear_accel_magnitude);
    RUN_TEST(test_isqrt);
    RUN_TEST(test_reset_forgets_estimate);
    return UNITY_END();
}
