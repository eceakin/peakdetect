#include "gesture_detector.h"

#include "app_config.h"
#include "gesture_classifier.h"

static det_state_t s_state;
static thresholds_t s_th;
static window_peaks_t s_peaks;
static pd_tick_t s_cooldown_until;
static uint32_t s_quiet_run; /* COOLDOWN/REARM'da art arda sakin örnek sayısı */

static int32_t abs32(int32_t v)
{
    return v < 0 ? -v : v;
}

static int32_t max_abs_w(const motion_t *m)
{
    int32_t a = abs32(m->w[0]), b = abs32(m->w[1]), c = abs32(m->w[2]);
    int32_t mx = a > b ? a : b;
    return mx > c ? mx : c;
}

/*
 * Sinyal, tetik eşiklerinin pct yüzdesine ulaştı mı?
 * pct = 100 -> tetik, pct = PD_REARM_PCT -> sakinlik kontrolü.
 * Kesirsiz: x * 100 >= esik * pct   (en büyük çarpım ~2.8e6, int32 yeterli)
 */
static bool above(const motion_t *m, int32_t pct)
{
    return m->a_lin_mag * 100 >= s_th.trigger_mg * pct ||
           max_abs_w(m) * 100 >= s_th.trigger_w * pct;
}

static void peaks_clear(void)
{
    window_peaks_t z = {0};
    s_peaks = z;
}

static void peaks_add(const motion_t *m, bool saturated)
{
    for (int i = 0; i < 3; i++) {
        const int32_t a = m->a_lin[i];
        if (a > s_peaks.a_pos[i]) {
            s_peaks.a_pos[i] = a;
        }
        if (abs32(a) > s_peaks.a_abs[i]) {
            s_peaks.a_abs[i] = abs32(a);
        }
        if (abs32(m->w[i]) > s_peaks.w_abs[i]) {
            s_peaks.w_abs[i] = abs32(m->w[i]);
        }
    }
    if (m->a_lin_mag > s_peaks.a_mag) {
        s_peaks.a_mag = m->a_lin_mag;
    }
    s_peaks.saturated = s_peaks.saturated || saturated;
    s_peaks.n_samples++;
}

void detector_init(const thresholds_t *th)
{
    s_th = *th;
    s_state = DET_IDLE;
    s_cooldown_until = 0;
    s_quiet_run = 0;
    peaks_clear();
}

/* Sakinlik sayacı: tetik eşiğinin %70'inin altındaki her örnek +1, üstündeki sıfırlar */
static void count_quiet(const motion_t *m)
{
    if (above(m, PD_REARM_PCT)) {
        s_quiet_run = 0;
    } else if (s_quiet_run < PD_REARM_QUIET_TICKS) {
        s_quiet_run++;
    }
}

void detector_set_thresholds(const thresholds_t *th)
{
    s_th = *th;
}

bool detector_update(const motion_t *m, bool saturated, pd_tick_t now, gesture_result_t *out)
{
    switch (s_state) {
    case DET_IDLE:
        if (above(m, 100)) {
            peaks_clear();
            peaks_add(m, saturated);
            s_state = DET_WINDOW;
        }
        return false;

    case DET_WINDOW:
        peaks_add(m, saturated);
        if (s_peaks.n_samples < PD_WINDOW_SAMPLES) {
            return false;
        }
        classifier_classify(&s_peaks, &s_th, now, out);
        s_quiet_run = 0;
        if (out->gesture != GESTURE_NONE) {
            s_cooldown_until = now + PD_COOLDOWN_TICKS;
            s_state = DET_COOLDOWN;
        } else {
            s_state = DET_REARM;
        }
        return true;

    case DET_COOLDOWN:
        /* Sakinlik cooldown boyunca da sayılır: el sakinse süre dolunca hemen IDLE */
        count_quiet(m);
        /* Taşmaya dayanıklı karşılaştırma: tick sayacı başa dönse de doğru çalışır */
        if ((int32_t)(now - s_cooldown_until) < 0) {
            return false;
        }
        s_state = DET_REARM;
        if (s_quiet_run >= PD_REARM_QUIET_TICKS) {
            s_state = DET_IDLE;
        }
        return false;

    case DET_REARM:
        count_quiet(m);
        if (s_quiet_run >= PD_REARM_QUIET_TICKS) {
            s_state = DET_IDLE;
        }
        return false;

    default:
        s_state = DET_IDLE;
        return false;
    }
}

bool detector_wants_freeze(void)
{
    return s_state == DET_WINDOW;
}

det_state_t detector_state(void)
{
    return s_state;
}

const window_peaks_t *detector_last_peaks(void)
{
    return &s_peaks;
}
