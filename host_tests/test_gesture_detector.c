#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "gesture_detector.h"
#include "gravity_filter.h"
#include "imu_convert.h"
#include "sensitivity.h"
#include "signals.h"
#include "unity.h"

void setUp(void)
{
    thresholds_t th;
    sensitivity_apply(SENS_MED, &th);
    detector_init(&th);
    gravity_reset();
}
void tearDown(void) {}

/* ================================================================== */
/* A) Durum makinesi: elle hazırlanmış motion_t ile                    */
/* ================================================================== */

static motion_t quiet(void)
{
    motion_t m = {0};
    return m;
}

/* İleri itme gibi görünen tek örnek (Medium'da FIRE kuralını geçer) */
static motion_t push(int32_t x_mg)
{
    motion_t m = {0};
    m.a_lin[AX_X] = x_mg;
    m.a_lin_mag = x_mg < 0 ? -x_mg : x_mg;
    return m;
}

/* t0'dan itibaren 12 örnek itme besler; karar verilen tick'i döndürür (-1: yok) */
static long feed_push_window(pd_tick_t t0, int32_t x_mg, gesture_result_t *r)
{
    for (int k = 0; k < PD_WINDOW_SAMPLES; k++) {
        motion_t m = push(x_mg);
        if (detector_update(&m, false, t0 + (pd_tick_t)k, r)) {
            return (long)(t0 + (pd_tick_t)k);
        }
    }
    return -1;
}

static void test_stays_idle_below_trigger(void)
{
    gesture_result_t r;
    for (pd_tick_t t = 0; t < 1000; t++) {
        motion_t m = push(799); /* tetik 800 mg */
        TEST_ASSERT_FALSE(detector_update(&m, false, t, &r));
        TEST_ASSERT_EQUAL(DET_IDLE, detector_state());
        TEST_ASSERT_FALSE(detector_wants_freeze());
    }
}

static void test_rotation_alone_can_trigger(void)
{
    gesture_result_t r;
    motion_t m = quiet();
    m.w[AX_Y] = -PD_TH_TRIGGER_W; /* işaret önemsiz */
    detector_update(&m, false, 0, &r);
    TEST_ASSERT_EQUAL(DET_WINDOW, detector_state());
}

/* Pencere tam 12 örnek; ilk örnek tetikleyen örnek. Pencere boyunca dondurma isteği. */
static void test_window_is_12_samples_with_freeze(void)
{
    gesture_result_t r;
    const pd_tick_t t0 = 10;
    for (int k = 0; k < PD_WINDOW_SAMPLES; k++) {
        motion_t m = push(2000);
        bool done = detector_update(&m, false, t0 + (pd_tick_t)k, &r);
        if (k < PD_WINDOW_SAMPLES - 1) {
            TEST_ASSERT_FALSE(done);
            TEST_ASSERT_TRUE(detector_wants_freeze());
        } else {
            TEST_ASSERT_TRUE(done);
            TEST_ASSERT_EQUAL(GESTURE_FIRE, r.gesture);
            TEST_ASSERT_EQUAL_UINT32(t0 + 11, r.t);
            TEST_ASSERT_FALSE(detector_wants_freeze());
        }
    }
    TEST_ASSERT_EQUAL(PD_WINDOW_SAMPLES, detector_last_peaks()->n_samples);
    TEST_ASSERT_EQUAL_INT32(2000, detector_last_peaks()->a_pos[AX_X]);
}

/* Yol haritası: 500 ms içinde ikinci tetik yok — tam 100 tick */
static void test_cooldown_is_exactly_500ms(void)
{
    gesture_result_t r;
    long T = feed_push_window(0, 2000, &r);
    TEST_ASSERT_EQUAL_INT32(11, T);
    TEST_ASSERT_EQUAL(DET_COOLDOWN, detector_state());

    /* Cooldown boyunca güçlü sinyal bile pencere açmaz */
    pd_tick_t t = (pd_tick_t)T + 1;
    for (; t <= (pd_tick_t)T + PD_COOLDOWN_TICKS; t++) {
        motion_t m = push(3000);
        TEST_ASSERT_FALSE(detector_update(&m, false, t, &r));
        TEST_ASSERT_NOT_EQUAL(DET_WINDOW, detector_state());
    }
    /* Süre doldu ama el hâlâ hareketli -> REARM */
    TEST_ASSERT_EQUAL(DET_REARM, detector_state());

    /* 10 sakin örnek sonra IDLE, sonraki örnek yeni pencere açabilir */
    motion_t q = quiet();
    for (int k = 1; k <= PD_REARM_QUIET_TICKS; k++, t++) {
        detector_update(&q, false, t, &r);
        TEST_ASSERT_EQUAL(k < PD_REARM_QUIET_TICKS ? DET_REARM : DET_IDLE, detector_state());
    }
    motion_t m = push(2000);
    detector_update(&m, false, t, &r);
    TEST_ASSERT_EQUAL(DET_WINDOW, detector_state());
}

/* El cooldown boyunca sakinse süre dolduğu örnekte IDLE (cooldown uzamaz) */
static void test_quiet_hand_idle_exactly_after_cooldown(void)
{
    gesture_result_t r;
    long T = feed_push_window(0, 2000, &r);
    motion_t q = quiet();
    for (pd_tick_t k = 1; k < PD_COOLDOWN_TICKS; k++) {
        detector_update(&q, false, (pd_tick_t)T + k, &r);
        TEST_ASSERT_EQUAL(DET_COOLDOWN, detector_state());
    }
    detector_update(&q, false, (pd_tick_t)T + PD_COOLDOWN_TICKS, &r);
    TEST_ASSERT_EQUAL(DET_IDLE, detector_state());
}

/* Hızlanma ile yavaşlama arasındaki tek sakin örnek yeniden kurmaya yetmez */
static void test_single_zero_crossing_does_not_rearm(void)
{
    gesture_result_t r;
    feed_push_window(0, 1000, &r); /* ret -> REARM */
    pd_tick_t t = 12;
    motion_t q = quiet();
    motion_t back = push(-2500); /* yavaşlama evresi */

    detector_update(&q, false, t++, &r); /* sıfır geçişi */
    TEST_ASSERT_EQUAL(DET_REARM, detector_state());
    for (int k = 0; k < 20; k++) {
        TEST_ASSERT_FALSE(detector_update(&back, false, t++, &r));
        TEST_ASSERT_EQUAL(DET_REARM, detector_state()); /* yeni pencere açılmadı */
    }
}

/* Ret edilen pencere cooldown'a girmez, sinyal sakinleşince hemen IDLE */
static void test_rejected_window_skips_cooldown(void)
{
    gesture_result_t r;
    long T = feed_push_window(0, 1000, &r); /* tetik geçer, FIRE (1500) geçmez */
    TEST_ASSERT_EQUAL_INT32(11, T);
    TEST_ASSERT_EQUAL(GESTURE_NONE, r.gesture);
    TEST_ASSERT_EQUAL(REJECT_NO_RULE, r.reject);
    TEST_ASSERT_EQUAL(DET_REARM, detector_state());

    /* cooldown yok: 10 sakin örnek (50 ms) yeterli */
    motion_t q = quiet();
    for (int k = 1; k <= PD_REARM_QUIET_TICKS; k++) {
        detector_update(&q, false, (pd_tick_t)(11 + k), &r);
    }
    TEST_ASSERT_EQUAL(DET_IDLE, detector_state());
}

/* Histerezis: tetik eşiğinin %70'i ve üstü "hâlâ hareket" sayılır */
static void test_rearm_hysteresis(void)
{
    gesture_result_t r;
    feed_push_window(0, 1000, &r);
    TEST_ASSERT_EQUAL(DET_REARM, detector_state());

    const int32_t at70 = PD_TH_TRIGGER_MG * PD_REARM_PCT / 100; /* 560 mg */
    for (pd_tick_t t = 12; t < 200; t++) {
        motion_t m = push(at70);
        detector_update(&m, false, t, &r);
        TEST_ASSERT_EQUAL(DET_REARM, detector_state());
    }
    motion_t m = push(at70 - 1);
    for (int k = 1; k <= PD_REARM_QUIET_TICKS; k++) {
        detector_update(&m, false, (pd_tick_t)(199 + k), &r);
        TEST_ASSERT_EQUAL(k < PD_REARM_QUIET_TICKS ? DET_REARM : DET_IDLE, detector_state());
    }
}

/* Doymuş tek örnek bütün pencereyi geçersiz kılar */
static void test_saturated_sample_rejects_window(void)
{
    gesture_result_t r;
    for (int k = 0; k < PD_WINDOW_SAMPLES; k++) {
        motion_t m = push(2000);
        if (detector_update(&m, k == 5, (pd_tick_t)k, &r)) {
            break;
        }
    }
    TEST_ASSERT_EQUAL(REJECT_SATURATED, r.reject);
    TEST_ASSERT_TRUE(detector_last_peaks()->saturated);
}

/* Tick sayacı 32 bit sınırında başa dönse de cooldown 100 tick sürer */
static void test_cooldown_survives_tick_wraparound(void)
{
    gesture_result_t r;
    const pd_tick_t t0 = 0xFFFFFFF0u;
    long dummy = feed_push_window(t0, 2000, &r);
    (void)dummy;
    pd_tick_t T = r.t; /* 0xFFFFFFFB */
    TEST_ASSERT_EQUAL(DET_COOLDOWN, detector_state());

    motion_t q = quiet();
    for (pd_tick_t k = 1; k < PD_COOLDOWN_TICKS; k++) {
        detector_update(&q, false, T + k, &r); /* T + k 0'ın üstünden geçer */
        TEST_ASSERT_EQUAL(DET_COOLDOWN, detector_state());
    }
    detector_update(&q, false, T + PD_COOLDOWN_TICKS, &r);
    TEST_ASSERT_EQUAL(DET_IDLE, detector_state());
}

/* Hassasiyet değişikliği hemen etkili */
static void test_threshold_change_applies(void)
{
    gesture_result_t r;
    thresholds_t low;
    sensitivity_apply(SENS_LOW, &low);
    detector_set_thresholds(&low);

    feed_push_window(0, 1800, &r); /* MED'de FIRE olurdu, LOW'da 1950 gerekir */
    TEST_ASSERT_EQUAL(GESTURE_NONE, r.gesture);
}

/* ================================================================== */
/* B) Uçtan uca: yapay ham sinyal -> convert -> gravity -> detector    */
/* ================================================================== */

#define MAX_EV 32

typedef struct {
    gesture_t g[MAX_EV];
    pd_tick_t t[MAX_EV];
    int n_events;  /* kabul edilen hareket sayısı */
    int n_windows; /* açılan pencere sayısı (kabul + ret) */
    reject_reason_t last_reject;
} run_t;

static void run_pipeline(const imu_raw_t *buf, int n, sensitivity_t sens, run_t *out)
{
    thresholds_t th;
    sensitivity_apply(sens, &th);
    detector_init(&th);
    gravity_reset();
    memset(out, 0, sizeof *out);

    for (int i = 0; i < n; i++) {
        imu_sample_t s;
        motion_t m;
        gesture_result_t r;
        imu_convert(&buf[i], NULL, &s);
        gravity_update(&s, detector_wants_freeze(), &m);
        if (detector_update(&m, s.saturated, (pd_tick_t)i, &r)) {
            out->n_windows++;
            if (r.gesture != GESTURE_NONE && out->n_events < MAX_EV) {
                out->g[out->n_events] = r.gesture;
                out->t[out->n_events] = r.t;
                out->n_events++;
            } else {
                out->last_reject = r.reject;
            }
        }
    }
}

static imu_raw_t BUF[SIG_HZ * 130]; /* 130 s'ye kadar */

/* Tek hareket: başta 1 s sakin, hareket, sonra 2 s sakin */
static void one_gesture(gesture_t g, int strength, sensitivity_t sens, run_t *r)
{
    const int n = SIG_HZ * 4;
    signals_t s;
    sig_begin(&s, BUF, n, 10, 1);
    sig_gesture(&s, SIG_HZ, g, strength);
    sig_end(&s);
    run_pipeline(BUF, n, sens, r);
}

static void test_still_hand_no_windows(void)
{
    const int n = SIG_HZ * 10;
    signals_t s;
    sig_begin(&s, BUF, n, 20, 3);
    sig_end(&s);
    run_t r;
    run_pipeline(BUF, n, SENS_HIGH, &r); /* en hassas seviyede bile */
    TEST_ASSERT_EQUAL_INT(0, r.n_windows);
}

/* Yol haritası: her hareket doğru sınıfta — üç seviyede */
static void test_each_gesture_recognized_at_each_level(void)
{
    const gesture_t gs[] = {GESTURE_WEB, GESTURE_ICE, GESTURE_FIRE};
    const sensitivity_t lv[] = {SENS_LOW, SENS_MED, SENS_HIGH};
    const int strength[] = {120, 100, 100}; /* Low daha sert hareket ister */
    char msg[64];

    for (int l = 0; l < 3; l++) {
        for (int g = 0; g < 3; g++) {
            run_t r;
            one_gesture(gs[g], strength[l], lv[l], &r);
            snprintf(msg, sizeof msg, "seviye %d, hareket %d", (int)lv[l], (int)gs[g]);
            TEST_ASSERT_EQUAL_INT_MESSAGE(1, r.n_events, msg);
            TEST_ASSERT_EQUAL_MESSAGE(gs[g], r.g[0], msg);
            TEST_ASSERT_EQUAL_INT_MESSAGE(1, r.n_windows, msg); /* kuyruk yeni pencere açmadı */
        }
    }
}

/* Gecikme: hareketin başından karara <= 100 ms (hedef), pratikte ~70 ms */
static void test_latency_under_100ms(void)
{
    const gesture_t gs[] = {GESTURE_WEB, GESTURE_ICE, GESTURE_FIRE};
    for (int g = 0; g < 3; g++) {
        run_t r;
        one_gesture(gs[g], 100, SENS_MED, &r);
        TEST_ASSERT_EQUAL_INT(1, r.n_events);
        long latency_ticks = (long)r.t[0] - SIG_HZ; /* hareket SIG_HZ'de başladı */
        TEST_ASSERT_GREATER_OR_EQUAL_INT32(PD_WINDOW_SAMPLES - 1, latency_ticks);
        TEST_ASSERT_LESS_OR_EQUAL_INT32(PD_MS_TO_TICKS(100), latency_ticks);
    }
}

/* Yol haritası: gürültüde / normal harekette tetik yok (2 dakika) */
static void test_normal_arm_movement_two_minutes(void)
{
    const int n = SIG_HZ * 120;
    const sensitivity_t lv[] = {SENS_LOW, SENS_MED, SENS_HIGH};
    for (int l = 0; l < 3; l++) {
        signals_t s;
        sig_begin(&s, BUF, n, 15, 11);
        sig_normal_motion(&s, 0, n);
        sig_end(&s);
        run_t r;
        run_pipeline(BUF, n, lv[l], &r);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, r.n_events, "normal harekette yanlis tetik");
    }
}

/* Yol haritası: 500 ms içinde ikinci tetik yok */
static void test_two_pushes_300ms_apart_give_one_event(void)
{
    const int n = SIG_HZ * 4;
    signals_t s;
    sig_begin(&s, BUF, n, 10, 5);
    sig_gesture(&s, SIG_HZ, GESTURE_FIRE, 100);
    sig_gesture(&s, SIG_HZ + SIG_MS(300), GESTURE_FIRE, 100);
    sig_end(&s);
    run_t r;
    run_pipeline(BUF, n, SENS_MED, &r);
    TEST_ASSERT_EQUAL_INT(1, r.n_events);
}

static void test_two_pushes_800ms_apart_give_two_events(void)
{
    const int n = SIG_HZ * 4;
    signals_t s;
    sig_begin(&s, BUF, n, 10, 5);
    sig_gesture(&s, SIG_HZ, GESTURE_FIRE, 100);
    sig_gesture(&s, SIG_HZ + SIG_MS(800), GESTURE_FIRE, 100);
    sig_end(&s);
    run_t r;
    run_pipeline(BUF, n, SENS_MED, &r);
    TEST_ASSERT_EQUAL_INT(2, r.n_events);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(PD_COOLDOWN_TICKS, r.t[1] - r.t[0]);
}

/* Üç hareket arka arkaya, doğru sırada */
static void test_sequence_web_ice_fire(void)
{
    const int n = SIG_HZ * 6;
    signals_t s;
    sig_begin(&s, BUF, n, 10, 9);
    sig_gesture(&s, SIG_HZ * 1, GESTURE_WEB, 100);
    sig_gesture(&s, SIG_HZ * 2 + SIG_HZ / 2, GESTURE_ICE, 100);
    sig_gesture(&s, SIG_HZ * 4, GESTURE_FIRE, 100);
    sig_end(&s);
    run_t r;
    run_pipeline(BUF, n, SENS_MED, &r);
    TEST_ASSERT_EQUAL_INT(3, r.n_events);
    TEST_ASSERT_EQUAL(GESTURE_WEB, r.g[0]);
    TEST_ASSERT_EQUAL(GESTURE_ICE, r.g[1]);
    TEST_ASSERT_EQUAL(GESTURE_FIRE, r.g[2]);
}

/* Zayıf hareket: pencere açılır ama reddedilir, sistem IDLE'a döner */
static void test_weak_push_rejected_then_idle(void)
{
    run_t r;
    one_gesture(GESTURE_FIRE, 45, SENS_MED, &r); /* ~1.1 g: tetik var, kural yok */
    TEST_ASSERT_EQUAL_INT(0, r.n_events);
    TEST_ASSERT_EQUAL_INT(1, r.n_windows);
    TEST_ASSERT_EQUAL(REJECT_NO_RULE, r.last_reject);
    TEST_ASSERT_EQUAL(DET_IDLE, detector_state());
}

/* Çok sert hareket sensörü doyurur: kabul edilmez, yavaşlama evresi yeni pencere açmaz */
static void test_saturating_push_rejected(void)
{
    run_t r;
    one_gesture(GESTURE_FIRE, 400, SENS_MED, &r); /* 10 g > ±8 g aralığı */
    TEST_ASSERT_EQUAL_INT(0, r.n_events);
    TEST_ASSERT_EQUAL_INT(1, r.n_windows);
    TEST_ASSERT_EQUAL(REJECT_SATURATED, r.last_reject);
}

/* Normal hareketin ortasında yapılan itme yine tanınır */
static void test_push_during_normal_motion(void)
{
    const int n = SIG_HZ * 6;
    signals_t s;
    sig_begin(&s, BUF, n, 15, 21);
    sig_normal_motion(&s, 0, n);
    sig_gesture(&s, SIG_HZ * 3, GESTURE_FIRE, 100);
    sig_end(&s);
    run_t r;
    run_pipeline(BUF, n, SENS_MED, &r);
    TEST_ASSERT_EQUAL_INT(1, r.n_events);
    TEST_ASSERT_EQUAL(GESTURE_FIRE, r.g[0]);
}

int main(void)
{
    UNITY_BEGIN();
    /* durum makinesi */
    RUN_TEST(test_stays_idle_below_trigger);
    RUN_TEST(test_rotation_alone_can_trigger);
    RUN_TEST(test_window_is_12_samples_with_freeze);
    RUN_TEST(test_cooldown_is_exactly_500ms);
    RUN_TEST(test_quiet_hand_idle_exactly_after_cooldown);
    RUN_TEST(test_single_zero_crossing_does_not_rearm);
    RUN_TEST(test_rejected_window_skips_cooldown);
    RUN_TEST(test_rearm_hysteresis);
    RUN_TEST(test_saturated_sample_rejects_window);
    RUN_TEST(test_cooldown_survives_tick_wraparound);
    RUN_TEST(test_threshold_change_applies);
    /* uçtan uca */
    RUN_TEST(test_still_hand_no_windows);
    RUN_TEST(test_each_gesture_recognized_at_each_level);
    RUN_TEST(test_latency_under_100ms);
    RUN_TEST(test_normal_arm_movement_two_minutes);
    RUN_TEST(test_two_pushes_300ms_apart_give_one_event);
    RUN_TEST(test_two_pushes_800ms_apart_give_two_events);
    RUN_TEST(test_sequence_web_ice_fire);
    RUN_TEST(test_weak_push_rejected_then_idle);
    RUN_TEST(test_saturating_push_rejected);
    RUN_TEST(test_push_during_normal_motion);
    return UNITY_END();
}
