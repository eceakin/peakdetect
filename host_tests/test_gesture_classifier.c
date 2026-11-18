#include <stdio.h>
#include <stdlib.h>

#include "app_config.h"
#include "gesture_classifier.h"
#include "sensitivity.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/*
 * Eşikler (bkz. test_sensitivity):
 *            web_w_min  web_a_max  ice_a_min  fire_a_min  baskınlık
 *   LOW         4550       1500       1560       1950        1.5
 *   MED         3500       1500       1200       1500        1.5
 *   HIGH        2625       1500        900       1125        1.5
 *
 * Birimler: ivme mg, dönüş 0.1 °/s (3500 = 350 °/s)
 */

#define ANY (-999) /* margin kontrol edilmeyecek */

typedef struct {
    const char *name;
    sensitivity_t sens;
    int32_t ax_pos, ax_abs; /* X: ileri */
    int32_t az_pos, az_abs; /* Z: yukarı */
    int32_t wy;             /* Y dönüşü (mutlak tepe) */
    int32_t amag;           /* |a_lin| tepe */
    bool sat;
    gesture_t want_g;
    reject_reason_t want_r;
    int want_margin;
} case_t;

/* clang-format off */
static const case_t CASES[] = {
/*   ad                                     seviye     ax+   |ax|   az+   |az|   wy    |a|   doy   beklenen       ret                  margin */
    {"hareketsiz",                          SENS_MED,     0,    0,     0,    0,    0,    0, false, GESTURE_NONE, REJECT_NO_RULE,      0},
    /* --- WEB --- */
    {"web: tam esik",                       SENS_MED,     0,    0,     0,    0, 3500,  600, false, GESTURE_WEB,  REJECT_NONE,         0},
    {"web: esigin 1 alti",                  SENS_MED,     0,    0,     0,    0, 3499,  600, false, GESTURE_NONE, REJECT_NO_RULE,      0},
    {"web: ivme 1499 (ust sinir alti)",     SENS_MED,     0,    0,   900,  900, 3500, 1499, false, GESTURE_WEB,  REJECT_NONE,         0},
    {"web: ivme tam 1500 -> sert",          SENS_MED,     0,    0,  1000, 1000, 4000, 1500, false, GESTURE_NONE, REJECT_WEB_TOO_HARD, 0},
    {"web: guclu silkme",                   SENS_MED,     0,    0,     0,    0, 7000,  500, false, GESTURE_WEB,  REJECT_NONE,       100},
    /* --- ICE --- */
    {"ice: tam esik",                       SENS_MED,   700,  700,  1200, 1200,    0, 1400, false, GESTURE_ICE,  REJECT_NONE,         0},
    {"ice: esigin 1 alti",                  SENS_MED,     0,    0,  1199, 1199,    0, 1199, false, GESTURE_NONE, REJECT_NO_RULE,      0},
    {"ice: baskinlik tam 1.5 -> gecmez",    SENS_MED,  1000, 1000,  1500, 1500,    0, 1800, false, GESTURE_NONE, REJECT_NO_RULE,      0},
    {"ice: baskinlik 1.5'in hemen ustu",    SENS_MED,   999,  999,  1500, 1500,    0, 1800, false, GESTURE_ICE,  REJECT_NONE,        25},
    {"ice: x geriye buyuk -> baskin degil", SENS_MED,     0, 1200,  1500, 1500,    0, 1900, false, GESTURE_NONE, REJECT_NO_RULE,      0},
    {"ice: karsi eksen sifir",              SENS_MED,     0,    0,  1200, 1200,    0, 1200, false, GESTURE_ICE,  REJECT_NONE,         0},
    {"eli indirmek ice degil",              SENS_MED,     0,    0,     0, 2000,    0, 2000, false, GESTURE_NONE, REJECT_NO_RULE,      0},
    /* --- FIRE --- */
    {"fire: tam esik",                      SENS_MED,  1500, 1500,   900,  900,    0, 1750, false, GESTURE_FIRE, REJECT_NONE,         0},
    {"fire: esigin 1 alti",                 SENS_MED,  1499, 1499,     0,    0,    0, 1499, false, GESTURE_NONE, REJECT_NO_RULE,      0},
    {"fire: baskinlik tam 1.5 -> gecmez",   SENS_MED,  1500, 1500,     0, 1000,    0, 1800, false, GESTURE_NONE, REJECT_NO_RULE,      0},
    {"fire: guclu itme",                    SENS_MED,  3000, 3000,   500,  500,    0, 3050, false, GESTURE_FIRE, REJECT_NONE,       100},
    {"geri cekmek fire degil",              SENS_MED,     0, 3000,     0,    0,    0, 3000, false, GESTURE_NONE, REJECT_NO_RULE,      0},
    {"fire: z asagi buyuk -> baskin degil", SENS_MED,  2000, 2000,     0, 1500,    0, 2500, false, GESTURE_NONE, REJECT_NO_RULE,      0},
    /* --- Doyma --- */
    {"doymus pencere (fire gibi)",          SENS_MED,  3000, 3000,     0,    0,    0, 3000,  true, GESTURE_NONE, REJECT_SATURATED,    0},
    {"doymus pencere (bos)",                SENS_MED,     0,    0,     0,    0,    0,    0,  true, GESTURE_NONE, REJECT_SATURATED,    0},
    /* --- İki kural birden --- */
    {"web+ice: fark tam 20 -> ice",         SENS_MED,     0,    0,  1450, 1450, 3500, 1450, false, GESTURE_ICE,  REJECT_NONE,        20},
    {"web+ice: fark 16 -> belirsiz",        SENS_MED,     0,    0,  1400, 1400, 3500, 1400, false, GESTURE_NONE, REJECT_AMBIGUOUS,  ANY},
    {"web+ice: web cok baskin",             SENS_MED,     0,    0,  1250, 1250, 7000, 1300, false, GESTURE_WEB,  REJECT_NONE,       100},
    {"web+fire: fark 6 -> belirsiz (HIGH)", SENS_HIGH, 1200, 1200,     0,    0, 2625, 1300, false, GESTURE_NONE, REJECT_AMBIGUOUS,  ANY},
    {"web+fire: fire baskin (HIGH)",        SENS_HIGH, 1450, 1450,     0,    0, 2625, 1490, false, GESTURE_FIRE, REJECT_NONE,        28},
    /* --- LOW: daha sert hareket gerekir --- */
    {"low: fire 1500 yetmez",               SENS_LOW,  1500, 1500,     0,    0,    0, 1500, false, GESTURE_NONE, REJECT_NO_RULE,      0},
    {"low: fire 1950",                      SENS_LOW,  1950, 1950,     0,    0,    0, 1950, false, GESTURE_FIRE, REJECT_NONE,         0},
    {"low: web 350 dps yetmez",             SENS_LOW,     0,    0,     0,    0, 3500,  500, false, GESTURE_NONE, REJECT_NO_RULE,      0},
    {"low: web 455 dps",                    SENS_LOW,     0,    0,     0,    0, 4550,  500, false, GESTURE_WEB,  REJECT_NONE,         0},
    {"low: ice 1560",                       SENS_LOW,     0,    0,  1560, 1560,    0, 1560, false, GESTURE_ICE,  REJECT_NONE,         0},
    {"low: donus yetmez -> sert degil",     SENS_LOW,     0,    0,     0,    0, 4000, 1600, false, GESTURE_NONE, REJECT_NO_RULE,      0},
    /* --- HIGH: daha hafif hareket yeter --- */
    {"high: ice 900",                       SENS_HIGH,    0,    0,   900,  900,    0,  900, false, GESTURE_ICE,  REJECT_NONE,         0},
    {"high: ice 899",                       SENS_HIGH,    0,    0,   899,  899,    0,  899, false, GESTURE_NONE, REJECT_NO_RULE,      0},
    {"high: web 262.5 dps",                 SENS_HIGH,    0,    0,     0,    0, 2625,  400, false, GESTURE_WEB,  REJECT_NONE,         0},
    {"high: web ivme fazla -> sert",        SENS_HIGH, 1000, 1000,  1000, 1000, 3000, 1600, false, GESTURE_NONE, REJECT_WEB_TOO_HARD, 0},
};
/* clang-format on */

#define N_CASES ((int)(sizeof CASES / sizeof CASES[0]))

static window_peaks_t peaks_from(const case_t *c)
{
    window_peaks_t p = {0};
    p.a_pos[AX_X] = c->ax_pos;
    p.a_abs[AX_X] = c->ax_abs;
    p.a_pos[AX_Z] = c->az_pos;
    p.a_abs[AX_Z] = c->az_abs;
    p.w_abs[AX_Y] = c->wy;
    p.a_mag = c->amag;
    p.saturated = c->sat;
    p.n_samples = PD_WINDOW_SAMPLES;
    return p;
}

static void test_case_table(void)
{
    char msg[128];
    for (int i = 0; i < N_CASES; i++) {
        const case_t *c = &CASES[i];
        thresholds_t th;
        sensitivity_apply(c->sens, &th);
        window_peaks_t p = peaks_from(c);
        gesture_result_t r;

        classifier_classify(&p, &th, 1234, &r);

        snprintf(msg, sizeof msg, "vaka %d: %s", i + 1, c->name);
        TEST_ASSERT_EQUAL_MESSAGE(c->want_g, r.gesture, msg);
        TEST_ASSERT_EQUAL_MESSAGE(c->want_r, r.reject, msg);
        if (c->want_margin != ANY) {
            TEST_ASSERT_EQUAL_INT_MESSAGE(c->want_margin, r.margin_pct, msg);
        }
    }
}

static void test_case_count_is_in_roadmap_range(void)
{
    TEST_ASSERT_TRUE(N_CASES >= 30 && N_CASES <= 40);
}

/* ================== Tek tek ayrıntılar ================== */

/* Silkme Y ekseninde olmalı: X ya da Z dönüşü Web sayılmaz */
static void test_rotation_on_wrong_axis_is_not_web(void)
{
    thresholds_t th;
    sensitivity_apply(SENS_MED, &th);
    window_peaks_t p = {0};
    p.w_abs[AX_X] = 6000;
    p.w_abs[AX_Z] = 6000;
    p.a_mag = 300;
    gesture_result_t r;
    classifier_classify(&p, &th, 0, &r);
    TEST_ASSERT_EQUAL(GESTURE_NONE, r.gesture);
    TEST_ASSERT_EQUAL(REJECT_NO_RULE, r.reject);
}

static void test_peak_and_time_are_reported(void)
{
    thresholds_t th;
    sensitivity_apply(SENS_MED, &th);
    window_peaks_t p = {0};
    p.a_pos[AX_X] = 2310;
    p.a_abs[AX_X] = 2310;
    p.a_mag = 2400;
    gesture_result_t r;
    classifier_classify(&p, &th, 9643, &r);
    TEST_ASSERT_EQUAL(GESTURE_FIRE, r.gesture);
    TEST_ASSERT_EQUAL_INT32(2400, r.peak_mg);
    TEST_ASSERT_EQUAL_UINT32(9643, r.t);
    TEST_ASSERT_EQUAL_INT16(54, r.margin_pct); /* (2310-1500)*100/1500 = 54 */

    /* Ret durumunda da tepe ve zaman dolu (analiz için) */
    p.saturated = true;
    classifier_classify(&p, &th, 100, &r);
    TEST_ASSERT_EQUAL_INT32(2400, r.peak_mg);
    TEST_ASSERT_EQUAL_UINT32(100, r.t);
}

static void test_ambiguous_reports_best_margin(void)
{
    thresholds_t th;
    sensitivity_apply(SENS_MED, &th);
    window_peaks_t p = {0};
    p.a_pos[AX_Z] = 1400; /* ice margin 16 */
    p.a_abs[AX_Z] = 1400;
    p.w_abs[AX_Y] = 3500; /* web margin 0 */
    p.a_mag = 1400;
    gesture_result_t r;
    classifier_classify(&p, &th, 0, &r);
    TEST_ASSERT_EQUAL(REJECT_AMBIGUOUS, r.reject);
    TEST_ASSERT_EQUAL_INT16(16, r.margin_pct);
}

static void test_margin_function(void)
{
    TEST_ASSERT_EQUAL_INT16(0, classifier_margin_pct(3500, 3500));
    TEST_ASSERT_EQUAL_INT16(100, classifier_margin_pct(7000, 3500));
    TEST_ASSERT_EQUAL_INT16(-50, classifier_margin_pct(600, 1200));
    TEST_ASSERT_EQUAL_INT16(20, classifier_margin_pct(1450, 1200)); /* 20.83 -> 20 */
    TEST_ASSERT_EQUAL_INT16(INT16_MAX, classifier_margin_pct(16000000, 1));
    TEST_ASSERT_EQUAL_INT16(0, classifier_margin_pct(1000, 0)); /* sıfıra bölme yok */
}

/* ================== Özellik testleri (rastgele 200 000 pencere) ================== */

static int32_t rnd(int32_t max) { return (int32_t)(rand() % (max + 1)); }

static window_peaks_t random_peaks(void)
{
    window_peaks_t p = {0};
    for (int i = 0; i < 3; i++) {
        p.a_pos[i] = rnd(4000);
        p.a_abs[i] = p.a_pos[i] + rnd(1000); /* |tepe| >= pozitif tepe */
        p.w_abs[i] = rnd(9000);
    }
    p.a_mag = p.a_abs[0] > p.a_abs[2] ? p.a_abs[0] : p.a_abs[2];
    p.a_mag += rnd(500);
    p.saturated = (rnd(50) == 0);
    p.n_samples = PD_WINDOW_SAMPLES;
    return p;
}

/*
 * Her seviyede:
 *  - ICE ve FIRE asla aynı anda geçmez
 *  - kabul edilen hareketin kuralı gerçekten geçmiştir
 *  - kabul varsa reject NONE, ret varsa gesture NONE
 *  - doymuş pencere asla kabul edilmez
 *  - aynı girdi -> aynı sonuç (durumsuz)
 */
static void test_properties_on_random_windows(void)
{
    srand(42);
    const sensitivity_t levels[] = {SENS_LOW, SENS_MED, SENS_HIGH};
    for (int n = 0; n < 200000; n++) {
        thresholds_t th;
        sensitivity_apply(levels[n % 3], &th);
        window_peaks_t p = random_peaks();
        gesture_result_t r1, r2;

        classifier_classify(&p, &th, (pd_tick_t)n, &r1);
        classifier_classify(&p, &th, (pd_tick_t)n, &r2);

        TEST_ASSERT_FALSE(classifier_rule_passes(GESTURE_ICE, &p, &th) &&
                          classifier_rule_passes(GESTURE_FIRE, &p, &th));
        if (r1.gesture != GESTURE_NONE) {
            TEST_ASSERT_TRUE(classifier_rule_passes(r1.gesture, &p, &th));
            TEST_ASSERT_EQUAL(REJECT_NONE, r1.reject);
            TEST_ASSERT_FALSE(p.saturated);
            TEST_ASSERT_GREATER_OR_EQUAL_INT16(0, r1.margin_pct);
        } else {
            TEST_ASSERT_NOT_EQUAL(REJECT_NONE, r1.reject);
        }
        TEST_ASSERT_EQUAL(r1.gesture, r2.gesture);
        TEST_ASSERT_EQUAL(r1.reject, r2.reject);
        TEST_ASSERT_EQUAL_INT16(r1.margin_pct, r2.margin_pct);
    }
}

/* Hassasiyet artınca kabul kaybolmamalı: MED'de kabul edilen her tek-kural penceresi
 * HIGH'da da aynı kuralı geçer (eşikler sadece düşer). */
static void test_higher_sensitivity_keeps_rule_passing(void)
{
    srand(7);
    thresholds_t med, high;
    sensitivity_apply(SENS_MED, &med);
    sensitivity_apply(SENS_HIGH, &high);
    for (int n = 0; n < 100000; n++) {
        window_peaks_t p = random_peaks();
        const gesture_t gs[] = {GESTURE_WEB, GESTURE_ICE, GESTURE_FIRE};
        for (int g = 0; g < 3; g++) {
            if (classifier_rule_passes(gs[g], &p, &med)) {
                TEST_ASSERT_TRUE(classifier_rule_passes(gs[g], &p, &high));
            }
        }
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_case_table);
    RUN_TEST(test_case_count_is_in_roadmap_range);
    RUN_TEST(test_rotation_on_wrong_axis_is_not_web);
    RUN_TEST(test_peak_and_time_are_reported);
    RUN_TEST(test_ambiguous_reports_best_margin);
    RUN_TEST(test_margin_function);
    RUN_TEST(test_properties_on_random_windows);
    RUN_TEST(test_higher_sensitivity_keeps_rule_passing);
    return UNITY_END();
}
