#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "frame_codec.h"
#include "replay.h"
#include "signals.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

#define N_DEMO (SIG_HZ * 12)
static imu_raw_t BUF[N_DEMO];
static uint8_t FLAGS[N_DEMO];

/* gen_signals ile aynı içerik: WEB 1 s, ICE 3 s, FIRE 5 s, 7-12 s normal hareket */
static void make_demo(void)
{
    signals_t s;
    sig_begin(&s, BUF, N_DEMO, 10, 2026);
    sig_gesture(&s, SIG_HZ * 1, GESTURE_WEB, 100);
    sig_gesture(&s, SIG_HZ * 3, GESTURE_ICE, 100);
    sig_gesture(&s, SIG_HZ * 5, GESTURE_FIRE, 100);
    sig_normal_motion(&s, SIG_HZ * 7, SIG_HZ * 5);
    sig_end(&s);
    memset(FLAGS, 0, sizeof FLAGS);
    FLAGS[SIG_HZ * 1 - SIG_MS(100)] = PD_FLAG_BUTTON;
    FLAGS[SIG_HZ * 3 - SIG_MS(100)] = PD_FLAG_BUTTON;
    FLAGS[SIG_HZ * 5 - SIG_MS(100)] = PD_FLAG_BUTTON;
}

static void write_demo_csv(const char *path)
{
    make_demo();
    FILE *fp = fopen(path, "w");
    TEST_ASSERT_NOT_NULL(fp);
    sig_write_csv(BUF, N_DEMO, FLAGS, fp);
    fclose(fp);
}

static int run_file(const char *in_path, const char *out_path, const replay_opts_t *o,
                    replay_stats_t *st)
{
    FILE *in = fopen(in_path, "r");
    FILE *out = fopen(out_path, "w");
    TEST_ASSERT_NOT_NULL(in);
    TEST_ASSERT_NOT_NULL(out);
    int rc = replay_run(in, out, o, st);
    fclose(in);
    fclose(out);
    return rc;
}

/* Çıktı dosyasının veri satırlarını (başlık hariç) okur */
static int read_lines(const char *path, char lines[][160], int max)
{
    FILE *fp = fopen(path, "r");
    TEST_ASSERT_NOT_NULL(fp);
    char buf[160];
    int n = -1; /* başlığı atla */
    while (fgets(buf, sizeof buf, fp) != NULL) {
        buf[strcspn(buf, "\r\n")] = '\0';
        if (n >= 0 && n < max) {
            strcpy(lines[n], buf);
        }
        n++;
    }
    fclose(fp);
    return n;
}

/* ================== Yol haritası: replay yapay CSV'de doğru hareketleri buluyor ================== */

static void test_demo_csv_finds_web_ice_fire(void)
{
    write_demo_csv("replay_demo.csv");
    replay_opts_t o;
    replay_default_opts(&o);
    replay_stats_t st;
    TEST_ASSERT_EQUAL_INT(0, run_file("replay_demo.csv", "replay_out.csv", &o, &st));

    TEST_ASSERT_EQUAL_INT32(N_DEMO, st.frames);
    TEST_ASSERT_EQUAL_INT32(0, st.bad_lines);
    TEST_ASSERT_EQUAL_INT32(1, st.events[GESTURE_WEB]);
    TEST_ASSERT_EQUAL_INT32(1, st.events[GESTURE_ICE]);
    TEST_ASSERT_EQUAL_INT32(1, st.events[GESTURE_FIRE]);

    char lines[8][160];
    TEST_ASSERT_EQUAL_INT(3, read_lines("replay_out.csv", lines, 8));

    const char *names[3] = {"WEB", "ICE", "FIRE"};
    const long starts[3] = {1000, 3000, 5000};
    for (int i = 0; i < 3; i++) {
        long t_ms;
        char name[8];
        int margin;
        TEST_ASSERT_EQUAL_INT(3, sscanf(lines[i], "%ld,%7[^,],%d", &t_ms, name, &margin));
        TEST_ASSERT_EQUAL_STRING(names[i], name);
        TEST_ASSERT_TRUE(t_ms >= starts[i] && t_ms <= starts[i] + 100); /* gecikme < 100 ms */
        TEST_ASSERT_GREATER_OR_EQUAL_INT(0, margin);
    }
}

/* --all: her pencere, tepe değerler ve buton işaretine göre zaman */
static void test_all_windows_mode_reports_peaks_and_marker(void)
{
    write_demo_csv("replay_demo.csv");
    replay_opts_t o;
    replay_default_opts(&o);
    o.all_windows = true;
    replay_stats_t st;
    TEST_ASSERT_EQUAL_INT(0, run_file("replay_demo.csv", "replay_all.csv", &o, &st));

    char lines[16][160];
    int n = read_lines("replay_all.csv", lines, 16);
    TEST_ASSERT_EQUAL_INT((int)st.windows, n);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(3, n);

    /* ilk pencere: WEB; işaret hareketten 100 ms önce -> since_marker 100..250 ms */
    long t_ms, peak, axp, axa, azp, aza, wy, since;
    int margin;
    char res[16], reason[16];
    TEST_ASSERT_EQUAL_INT(11, sscanf(lines[0], "%ld,%15[^,],%15[^,],%d,%ld,%ld,%ld,%ld,%ld,%ld,%ld",
                                     &t_ms, res, reason, &margin, &peak, &axp, &axa, &azp, &aza,
                                     &wy, &since));
    TEST_ASSERT_EQUAL_STRING("WEB", res);
    TEST_ASSERT_EQUAL_STRING("NONE", reason);
    TEST_ASSERT_GREATER_OR_EQUAL_INT32(3500, wy); /* 350 °/s eşiğinin üstü */
    TEST_ASSERT_TRUE(since >= 100 && since <= 250);
}

/* Sütun sırası serbest, fazladan sütun ve flags'in olmaması sorun değil, CRLF okunur */
static void test_column_order_extra_columns_and_crlf(void)
{
    const int n = SIG_HZ * 3;
    signals_t s;
    sig_begin(&s, BUF, n, 10, 77);
    sig_gesture(&s, SIG_HZ, GESTURE_FIRE, 100);
    sig_end(&s);

    FILE *fp = fopen("replay_cols.csv", "wb"); /* "wb": \r\n'yi biz yazıyoruz */
    TEST_ASSERT_NOT_NULL(fp);
    fprintf(fp, "time_s,gz,gy,gx,az,ay,ax,note\r\n");
    for (int i = 0; i < n; i++) {
        fprintf(fp, "%.3f,%d,%d,%d,%d,%d,%d,x\r\n", i / 200.0, BUF[i].gyro[2], BUF[i].gyro[1],
                BUF[i].gyro[0], BUF[i].accel[2], BUF[i].accel[1], BUF[i].accel[0]);
    }
    fclose(fp);

    replay_opts_t o;
    replay_default_opts(&o);
    replay_stats_t st;
    TEST_ASSERT_EQUAL_INT(0, run_file("replay_cols.csv", "replay_out.csv", &o, &st));
    TEST_ASSERT_EQUAL_INT32(0, st.bad_lines);
    TEST_ASSERT_EQUAL_INT32(1, st.events[GESTURE_FIRE]);
    TEST_ASSERT_EQUAL_INT32(1, st.windows);
}

static void test_missing_column_is_error(void)
{
    FILE *fp = fopen("replay_bad.csv", "w");
    fprintf(fp, "ax,ay,az,gx,gy\n1,2,3,4,5\n"); /* gz yok */
    fclose(fp);

    replay_opts_t o;
    replay_default_opts(&o);
    replay_stats_t st;
    TEST_ASSERT_EQUAL_INT(-1, run_file("replay_bad.csv", "replay_out.csv", &o, &st));

    fp = fopen("replay_empty.csv", "w");
    fclose(fp);
    TEST_ASSERT_EQUAL_INT(-1, run_file("replay_empty.csv", "replay_out.csv", &o, &st));
}

/* Bozuk satırlar atlanır ama zaman kaymaz: hareket aynı tick'te bulunur */
static void test_bad_lines_skipped_without_time_shift(void)
{
    const int n = SIG_HZ * 3;
    signals_t s;
    sig_begin(&s, BUF, n, 10, 31);
    sig_gesture(&s, SIG_HZ, GESTURE_ICE, 100);
    sig_end(&s);

    /* temiz dosya */
    FILE *fp = fopen("replay_clean.csv", "w");
    sig_write_csv(BUF, n, NULL, fp);
    fclose(fp);

    /* aynı dosya, hareketten önceki iki satır bozuk */
    fp = fopen("replay_dirty.csv", "w");
    fprintf(fp, "seq,flags,ax,ay,az,gx,gy,gz\n");
    for (int i = 0; i < n; i++) {
        if (i == 50) {
            fprintf(fp, "abc,def\n");
        } else if (i == 60) {
            fprintf(fp, "60,0,99999,0,4096,0,0,0\n"); /* int16 dışı */
        } else {
            fprintf(fp, "%d,0,%d,%d,%d,%d,%d,%d\n", i & 0xFF, BUF[i].accel[0], BUF[i].accel[1],
                    BUF[i].accel[2], BUF[i].gyro[0], BUF[i].gyro[1], BUF[i].gyro[2]);
        }
    }
    fclose(fp);

    replay_opts_t o;
    replay_default_opts(&o);
    replay_stats_t st;
    char clean[4][160], dirty[4][160];

    run_file("replay_clean.csv", "replay_out_clean.csv", &o, &st);
    TEST_ASSERT_EQUAL_INT(1, read_lines("replay_out_clean.csv", clean, 4));

    run_file("replay_dirty.csv", "replay_out_dirty.csv", &o, &st);
    TEST_ASSERT_EQUAL_INT32(2, st.bad_lines);
    TEST_ASSERT_EQUAL_INT(1, read_lines("replay_out_dirty.csv", dirty, 4));

    TEST_ASSERT_EQUAL_STRING(clean[0], dirty[0]); /* aynı zaman, aynı tür, aynı margin */
}

/* Hassasiyet seçeneği replay'e ulaşıyor */
static void test_sensitivity_option(void)
{
    const int n = SIG_HZ * 3;
    signals_t s;
    sig_begin(&s, BUF, n, 10, 41);
    sig_gesture(&s, SIG_HZ, GESTURE_FIRE, 70); /* ~1.75 g: MED'de geçer, LOW'da (1.95 g) geçmez */
    sig_end(&s);
    FILE *fp = fopen("replay_sens.csv", "w");
    sig_write_csv(BUF, n, NULL, fp);
    fclose(fp);

    replay_opts_t o;
    replay_default_opts(&o);
    replay_stats_t st;

    o.sens = SENS_MED;
    run_file("replay_sens.csv", "replay_out.csv", &o, &st);
    TEST_ASSERT_EQUAL_INT32(1, st.events[GESTURE_FIRE]);

    o.sens = SENS_LOW;
    run_file("replay_sens.csv", "replay_out.csv", &o, &st);
    TEST_ASSERT_EQUAL_INT32(0, st.events[GESTURE_FIRE]);
    TEST_ASSERT_EQUAL_INT32(1, st.rejects[REJECT_NO_RULE]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_demo_csv_finds_web_ice_fire);
    RUN_TEST(test_all_windows_mode_reports_peaks_and_marker);
    RUN_TEST(test_column_order_extra_columns_and_crlf);
    RUN_TEST(test_missing_column_is_error);
    RUN_TEST(test_bad_lines_skipped_without_time_shift);
    RUN_TEST(test_sensitivity_option);
    return UNITY_END();
}
