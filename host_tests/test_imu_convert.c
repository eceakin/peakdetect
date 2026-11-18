#include "app_config.h"
#include "imu_convert.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static calib_t def(void)
{
    calib_t c;
    calib_set_default(&c);
    return c;
}

/* ================== İVME ================== */

/* Yol haritası: 4096 LSB -> 1000 mg */
static void test_accel_1g_is_1000mg(void)
{
    TEST_ASSERT_EQUAL_INT32(1000, imu_accel_to_mg(4096, 1024));
    TEST_ASSERT_EQUAL_INT32(-1000, imu_accel_to_mg(-4096, 1024));
    TEST_ASSERT_EQUAL_INT32(0, imu_accel_to_mg(0, 1024));
}

static void test_accel_full_scale_is_about_8g(void)
{
    TEST_ASSERT_INT32_WITHIN(1, 7999, imu_accel_to_mg(32767, 1024));
    TEST_ASSERT_EQUAL_INT32(-8000, imu_accel_to_mg(-32768, 1024));
}

/* Ölçek düzeltmesi: 1.05 ve 0.95 */
static void test_accel_scale_correction(void)
{
    TEST_ASSERT_INT32_WITHIN(1, 1050, imu_accel_to_mg(4096, 1075));
    TEST_ASSERT_INT32_WITHIN(1, 950, imu_accel_to_mg(4096, 973));
}

/* Kesme sıfıra doğru: işaret simetrik olmalı (yerçekimi filtresi kaymasın) */
static void test_accel_sign_symmetry(void)
{
    for (int raw = 0; raw <= 32767; raw += 7) {
        TEST_ASSERT_EQUAL_INT32(-imu_accel_to_mg((int16_t)raw, 1024),
                                imu_accel_to_mg((int16_t)-raw, 1024));
    }
}

/* Tüm 65 536 ham değer için kayan noktalı referansla en fazla 1 mg fark */
static void test_accel_matches_float_reference_everywhere(void)
{
    const int16_t scales[] = {973, 1024, 1075};
    for (int s = 0; s < 3; s++) {
        for (int raw = -32768; raw <= 32767; raw++) {
            double ref = raw * (scales[s] / 1024.0) * (1000.0 / 4096.0);
            int32_t got = imu_accel_to_mg((int16_t)raw, scales[s]);
            double diff = got - ref;
            if (diff > 1.0 || diff < -1.0) {
                TEST_FAIL_MESSAGE("ivme referanstan 1 mg'den fazla saptı");
            }
        }
    }
}

/* ================== DÖNÜŞ ================== */

/* Yol haritası: 655 LSB -> 100 (= 10.0 °/s) */
static void test_gyro_655_is_100(void)
{
    TEST_ASSERT_EQUAL_INT32(100, imu_gyro_to_dps10(655, 0));
    TEST_ASSERT_EQUAL_INT32(-100, imu_gyro_to_dps10(-655, 0));
}

static void test_gyro_350dps_threshold(void)
{
    /* 350 °/s = 22 925 LSB -> 3500 (Web eşiği) */
    TEST_ASSERT_EQUAL_INT32(3500, imu_gyro_to_dps10(22925, 0));
}

/* Yol haritası: bias'ın çıkarılması */
static void test_gyro_bias_is_subtracted(void)
{
    TEST_ASSERT_EQUAL_INT32(100, imu_gyro_to_dps10(700, 45));
    TEST_ASSERT_EQUAL_INT32(0, imu_gyro_to_dps10(-34, -34));
    TEST_ASSERT_EQUAL_INT32(-100, imu_gyro_to_dps10(-610, 45));
}

/* En uç değerler: taşma yok, int64 referansla aynı */
static void test_gyro_extremes_no_overflow(void)
{
    const int16_t raws[] = {-32768, 32767};
    const int16_t biases[] = {-1310, 1310};
    for (int r = 0; r < 2; r++) {
        for (int b = 0; b < 2; b++) {
            int64_t ref = ((int64_t)raws[r] - biases[b]) * 20 / 131;
            TEST_ASSERT_EQUAL_INT32((int32_t)ref, imu_gyro_to_dps10(raws[r], biases[b]));
        }
    }
}

static void test_gyro_matches_float_reference_everywhere(void)
{
    for (int raw = -32768; raw <= 32767; raw++) {
        double ref = raw / 6.55; /* 0.1 °/s */
        double diff = imu_gyro_to_dps10((int16_t)raw, 0) - ref;
        if (diff > 1.0 || diff < -1.0) {
            TEST_FAIL_MESSAGE("dönüş referanstan 0.1 °/s'den fazla saptı");
        }
    }
}

/* ================== imu_convert (tam örnek) ================== */

static void test_convert_board_flat(void)
{
    imu_raw_t raw = {.accel = {0, 0, 4096}, .gyro = {45, -20, 3}, .temp = 999};
    calib_t c = def();
    c.gyro_bias[0] = 45;
    c.gyro_bias[1] = -20;
    c.gyro_bias[2] = 3;
    imu_sample_t s;

    imu_convert(&raw, &c, &s);

    TEST_ASSERT_EQUAL_INT32(0, s.a[AX_X]);
    TEST_ASSERT_EQUAL_INT32(0, s.a[AX_Y]);
    TEST_ASSERT_EQUAL_INT32(1000, s.a[AX_Z]);
    TEST_ASSERT_EQUAL_INT32(0, s.w[AX_X]);
    TEST_ASSERT_EQUAL_INT32(0, s.w[AX_Y]);
    TEST_ASSERT_EQUAL_INT32(0, s.w[AX_Z]);
    TEST_ASSERT_FALSE(s.saturated);
}

/* Bias ivmeye, ölçek dönüşe uygulanmamalı (eksen/alan karışması olmasın) */
static void test_convert_bias_and_scale_on_right_fields(void)
{
    imu_raw_t raw = {.accel = {4096, 4096, 4096}, .gyro = {655, 655, 655}};
    calib_t c = def();
    c.gyro_bias[1] = 655;    /* sadece Y */
    c.accel_scale[2] = 1075; /* sadece Z */
    imu_sample_t s;

    imu_convert(&raw, &c, &s);

    TEST_ASSERT_EQUAL_INT32(1000, s.a[AX_X]);
    TEST_ASSERT_EQUAL_INT32(1000, s.a[AX_Y]);
    TEST_ASSERT_INT32_WITHIN(1, 1050, s.a[AX_Z]);
    TEST_ASSERT_EQUAL_INT32(100, s.w[AX_X]);
    TEST_ASSERT_EQUAL_INT32(0, s.w[AX_Y]);
    TEST_ASSERT_EQUAL_INT32(100, s.w[AX_Z]);
}

static void test_convert_null_calib_means_no_correction(void)
{
    imu_raw_t raw = {.accel = {-4096, 0, 0}, .gyro = {0, 0, 655}};
    imu_sample_t s;
    imu_convert(&raw, NULL, &s);
    TEST_ASSERT_EQUAL_INT32(-1000, s.a[AX_X]);
    TEST_ASSERT_EQUAL_INT32(100, s.w[AX_Z]);
}

/* ================== DOYMA ================== */

/* Yol haritası: ±32767'de doyma bayrağı */
static void test_saturation_flag(void)
{
    calib_t c = def();
    imu_sample_t s;

    imu_raw_t r1 = {.accel = {32767, 0, 0}};
    imu_convert(&r1, &c, &s);
    TEST_ASSERT_TRUE(s.saturated);

    imu_raw_t r2 = {.gyro = {0, 0, -32768}};
    imu_convert(&r2, &c, &s);
    TEST_ASSERT_TRUE(s.saturated);

    imu_raw_t r3 = {.accel = {0, -32767, 0}};
    imu_convert(&r3, &c, &s);
    TEST_ASSERT_TRUE(s.saturated);

    imu_raw_t r4 = {.accel = {32766, -32766, 0}, .gyro = {32766, 0, -32766}};
    imu_convert(&r4, &c, &s);
    TEST_ASSERT_FALSE(s.saturated);
}

/* Doyma ham değere bakar: bias yüzünden sınırın altına inen değer yine doymuş sayılır */
static void test_saturation_checked_before_bias(void)
{
    calib_t c = def();
    c.gyro_bias[0] = 100;
    imu_raw_t r = {.gyro = {32767, 0, 0}};
    imu_sample_t s;
    imu_convert(&r, &c, &s);
    TEST_ASSERT_TRUE(s.saturated);
}

/* ================== KALİBRASYON SINIRLARI ================== */

static void test_calib_default_is_valid(void)
{
    calib_t c = def();
    TEST_ASSERT_TRUE(calib_is_valid(&c));
    TEST_ASSERT_EQUAL_INT16(1024, c.accel_scale[0]);
    TEST_ASSERT_EQUAL_INT16(0, c.gyro_bias[2]);
}

static void test_calib_limits(void)
{
    calib_t c = def();

    c.gyro_bias[1] = PD_GYRO_BIAS_LIMIT_LSB;
    TEST_ASSERT_TRUE(calib_is_valid(&c));
    c.gyro_bias[1] = PD_GYRO_BIAS_LIMIT_LSB + 1;
    TEST_ASSERT_FALSE(calib_is_valid(&c));
    c.gyro_bias[1] = -PD_GYRO_BIAS_LIMIT_LSB - 1;
    TEST_ASSERT_FALSE(calib_is_valid(&c));

    c = def();
    c.accel_scale[2] = PD_ACCEL_SCALE_MIN_Q10 - 1;
    TEST_ASSERT_FALSE(calib_is_valid(&c));
    c.accel_scale[2] = PD_ACCEL_SCALE_MAX_Q10 + 1;
    TEST_ASSERT_FALSE(calib_is_valid(&c));
    c.accel_scale[2] = PD_ACCEL_SCALE_MAX_Q10;
    TEST_ASSERT_TRUE(calib_is_valid(&c));

    /* NVS'ten silinmiş flash (hepsi 0xFF) okunursa geçersiz olmalı */
    c.accel_scale[0] = -1;
    TEST_ASSERT_FALSE(calib_is_valid(&c));

    TEST_ASSERT_FALSE(calib_is_valid(NULL));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_accel_1g_is_1000mg);
    RUN_TEST(test_accel_full_scale_is_about_8g);
    RUN_TEST(test_accel_scale_correction);
    RUN_TEST(test_accel_sign_symmetry);
    RUN_TEST(test_accel_matches_float_reference_everywhere);
    RUN_TEST(test_gyro_655_is_100);
    RUN_TEST(test_gyro_350dps_threshold);
    RUN_TEST(test_gyro_bias_is_subtracted);
    RUN_TEST(test_gyro_extremes_no_overflow);
    RUN_TEST(test_gyro_matches_float_reference_everywhere);
    RUN_TEST(test_convert_board_flat);
    RUN_TEST(test_convert_bias_and_scale_on_right_fields);
    RUN_TEST(test_convert_null_calib_means_no_correction);
    RUN_TEST(test_saturation_flag);
    RUN_TEST(test_saturation_checked_before_bias);
    RUN_TEST(test_calib_default_is_valid);
    RUN_TEST(test_calib_limits);
    return UNITY_END();
}
