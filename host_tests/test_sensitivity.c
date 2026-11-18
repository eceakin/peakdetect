#include "app_config.h"
#include "sensitivity.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static thresholds_t get(sensitivity_t s)
{
    thresholds_t t;
    sensitivity_apply(s, &t);
    return t;
}

/* ================== Medium = tablo ================== */

static void test_medium_equals_base_table(void)
{
    thresholds_t base, med = get(SENS_MED);
    sensitivity_base(&base);

    TEST_ASSERT_EQUAL_INT32(PD_TH_TRIGGER_MG, base.trigger_mg);
    TEST_ASSERT_EQUAL_INT32(PD_TH_WEB_W_MIN, base.web_w_min);
    TEST_ASSERT_EQUAL_MEMORY(&base, &med, sizeof base);
}

/* Önerideki başlangıç değerleri (Bölüm 8) */
static void test_base_matches_proposal(void)
{
    thresholds_t t;
    sensitivity_base(&t);
    TEST_ASSERT_EQUAL_INT32(3500, t.web_w_min);  /* 350 °/s */
    TEST_ASSERT_EQUAL_INT32(1500, t.web_a_max);  /* 1.5 g   */
    TEST_ASSERT_EQUAL_INT32(1200, t.ice_a_min);  /* 1.2 g   */
    TEST_ASSERT_EQUAL_INT32(1500, t.fire_a_min); /* 1.5 g   */
    TEST_ASSERT_EQUAL_INT32(15, t.dominance_x10);
}

/* ================== Low = x1.3 ================== */

static void test_low_scales_lower_thresholds_by_130(void)
{
    thresholds_t t = get(SENS_LOW);
    TEST_ASSERT_EQUAL_INT32(1040, t.trigger_mg); /*  800 x 1.3 */
    TEST_ASSERT_EQUAL_INT32(3250, t.trigger_w);  /* 2500 x 1.3 */
    TEST_ASSERT_EQUAL_INT32(4550, t.web_w_min);  /* 3500 x 1.3 */
    TEST_ASSERT_EQUAL_INT32(1560, t.ice_a_min);  /* 1200 x 1.3 */
    TEST_ASSERT_EQUAL_INT32(1950, t.fire_a_min); /* 1500 x 1.3 */
}

/* ================== High = x0.75 ================== */

static void test_high_scales_lower_thresholds_by_75(void)
{
    thresholds_t t = get(SENS_HIGH);
    TEST_ASSERT_EQUAL_INT32(600, t.trigger_mg);
    TEST_ASSERT_EQUAL_INT32(1875, t.trigger_w);
    TEST_ASSERT_EQUAL_INT32(2625, t.web_w_min);
    TEST_ASSERT_EQUAL_INT32(900, t.ice_a_min);
    TEST_ASSERT_EQUAL_INT32(1125, t.fire_a_min);
}

/* ================== Ölçeklenmeyenler ================== */

/* Yol haritası: oranların ölçeklenmemesi */
static void test_dominance_ratio_not_scaled(void)
{
    TEST_ASSERT_EQUAL_INT32(PD_TH_DOMINANCE_X10, get(SENS_LOW).dominance_x10);
    TEST_ASSERT_EQUAL_INT32(PD_TH_DOMINANCE_X10, get(SENS_MED).dominance_x10);
    TEST_ASSERT_EQUAL_INT32(PD_TH_DOMINANCE_X10, get(SENS_HIGH).dominance_x10);
}

/* Adım 2 karar 4: Web'in üst sınırı sabit */
static void test_web_upper_limit_not_scaled(void)
{
    TEST_ASSERT_EQUAL_INT32(PD_TH_WEB_A_MAX, get(SENS_LOW).web_a_max);
    TEST_ASSERT_EQUAL_INT32(PD_TH_WEB_A_MAX, get(SENS_MED).web_a_max);
    TEST_ASSERT_EQUAL_INT32(PD_TH_WEB_A_MAX, get(SENS_HIGH).web_a_max);
}

/* ================== Tutarlılık ================== */

/* Low her alt eşikte en zor, High en kolay olmalı */
static void test_low_is_strictest_high_is_easiest(void)
{
    thresholds_t lo = get(SENS_LOW), me = get(SENS_MED), hi = get(SENS_HIGH);
    TEST_ASSERT_TRUE(lo.trigger_mg > me.trigger_mg && me.trigger_mg > hi.trigger_mg);
    TEST_ASSERT_TRUE(lo.trigger_w > me.trigger_w && me.trigger_w > hi.trigger_w);
    TEST_ASSERT_TRUE(lo.web_w_min > me.web_w_min && me.web_w_min > hi.web_w_min);
    TEST_ASSERT_TRUE(lo.ice_a_min > me.ice_a_min && me.ice_a_min > hi.ice_a_min);
    TEST_ASSERT_TRUE(lo.fire_a_min > me.fire_a_min && me.fire_a_min > hi.fire_a_min);
}

/*
 * Pencere zamanında açılmalı: her seviyede tetik eşiği kural eşiklerinden düşük.
 * Adım 8'de eşikler değişince bu test bozulursa, tetik eşiklerini de güncelleyin.
 */
static void test_trigger_below_rule_thresholds_at_every_level(void)
{
    const sensitivity_t levels[] = {SENS_LOW, SENS_MED, SENS_HIGH};
    for (int i = 0; i < 3; i++) {
        thresholds_t t = get(levels[i]);
        TEST_ASSERT_LESS_THAN_INT32(t.ice_a_min, t.trigger_mg);
        TEST_ASSERT_LESS_THAN_INT32(t.fire_a_min, t.trigger_mg);
        TEST_ASSERT_LESS_THAN_INT32(t.web_w_min, t.trigger_w);
    }
}

/* Web kuralı her seviyede mümkün olmalı: |a_lin| üst sınırı tetik ivmesinin üstünde */
static void test_web_rule_possible_at_every_level(void)
{
    const sensitivity_t levels[] = {SENS_LOW, SENS_MED, SENS_HIGH};
    for (int i = 0; i < 3; i++) {
        thresholds_t t = get(levels[i]);
        TEST_ASSERT_GREATER_THAN_INT32(t.trigger_mg, t.web_a_max);
    }
}

/* ================== Geçersiz değerler ve seviye değiştirme ================== */

static void test_invalid_level_falls_back_to_medium(void)
{
    thresholds_t bad = get((sensitivity_t)9), med = get(SENS_MED);
    TEST_ASSERT_EQUAL_MEMORY(&med, &bad, sizeof med);
    TEST_ASSERT_EQUAL_INT32(100, sensitivity_pct((sensitivity_t)9));
}

static void test_pct_values(void)
{
    TEST_ASSERT_EQUAL_INT32(130, sensitivity_pct(SENS_LOW));
    TEST_ASSERT_EQUAL_INT32(100, sensitivity_pct(SENS_MED));
    TEST_ASSERT_EQUAL_INT32(75, sensitivity_pct(SENS_HIGH));
}

/* Kısa basış: her seferinde tam bir adım, üç basışta başa döner */
static void test_next_cycles_through_levels(void)
{
    TEST_ASSERT_EQUAL(SENS_MED, sensitivity_next(SENS_LOW));
    TEST_ASSERT_EQUAL(SENS_HIGH, sensitivity_next(SENS_MED));
    TEST_ASSERT_EQUAL(SENS_LOW, sensitivity_next(SENS_HIGH));
    TEST_ASSERT_EQUAL(SENS_MED, sensitivity_next((sensitivity_t)200));

    sensitivity_t s = SENS_MED;
    for (int i = 0; i < 3; i++) {
        s = sensitivity_next(s);
    }
    TEST_ASSERT_EQUAL(SENS_MED, s);
}

static void test_is_valid(void)
{
    TEST_ASSERT_TRUE(sensitivity_is_valid(0));
    TEST_ASSERT_TRUE(sensitivity_is_valid(1));
    TEST_ASSERT_TRUE(sensitivity_is_valid(2));
    TEST_ASSERT_FALSE(sensitivity_is_valid(3));
    TEST_ASSERT_FALSE(sensitivity_is_valid(0xFF)); /* silinmiş flash */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_medium_equals_base_table);
    RUN_TEST(test_base_matches_proposal);
    RUN_TEST(test_low_scales_lower_thresholds_by_130);
    RUN_TEST(test_high_scales_lower_thresholds_by_75);
    RUN_TEST(test_dominance_ratio_not_scaled);
    RUN_TEST(test_web_upper_limit_not_scaled);
    RUN_TEST(test_low_is_strictest_high_is_easiest);
    RUN_TEST(test_trigger_below_rule_thresholds_at_every_level);
    RUN_TEST(test_web_rule_possible_at_every_level);
    RUN_TEST(test_invalid_level_falls_back_to_medium);
    RUN_TEST(test_pct_values);
    RUN_TEST(test_next_cycles_through_levels);
    RUN_TEST(test_is_valid);
    return UNITY_END();
}
