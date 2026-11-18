#include <stdio.h>
#include <string.h>

#include "frame_codec.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* Örnek bir çerçeve: pozitif, negatif ve uç değerler bir arada */
static log_frame_t sample_frame(void)
{
    log_frame_t f = {
        .seq = 42,
        .flags = GESTURE_FIRE | PD_FLAG_BUTTON | PD_FLAG_FROZEN,
        .accel = {4096, -4096, 0},
        .gyro = {32767, -32768, -1},
    };
    return f;
}

/* ================== ÇERÇEVE ================== */

static void test_pack_unpack_roundtrip(void)
{
    log_frame_t in = sample_frame(), out;
    uint8_t buf[PD_FRAME_LEN];

    frame_pack(&in, buf);
    TEST_ASSERT_EQUAL(FRAME_OK, frame_unpack(buf, &out));

    TEST_ASSERT_EQUAL_UINT8(in.seq, out.seq);
    TEST_ASSERT_EQUAL_UINT8(in.flags, out.flags);
    TEST_ASSERT_EQUAL_INT16_ARRAY(in.accel, out.accel, 3);
    TEST_ASSERT_EQUAL_INT16_ARRAY(in.gyro, out.gyro, 3);
}

/* Byte düzeni protocol.md ile birebir: elle hesaplanmış beklenen 16 byte */
static void test_exact_byte_layout(void)
{
    log_frame_t f = {
        .seq = 7,
        .flags = 0x01,
        .accel = {0x1234, -1, -32768},
        .gyro = {1, 256, 0},
    };
    uint8_t buf[PD_FRAME_LEN];
    frame_pack(&f, buf);

    const uint8_t expected[PD_FRAME_LEN] = {
        0xA5,       /* 0  sync          */
        0x07,       /* 1  seq           */
        0x01,       /* 2  flags         */
        0x34, 0x12, /* 3-4  ax = 0x1234 */
        0xFF, 0xFF, /* 5-6  ay = -1     */
        0x00, 0x80, /* 7-8  az = -32768 */
        0x01, 0x00, /* 9-10 gx = 1      */
        0x00, 0x01, /* 11-12 gy = 256   */
        0x00, 0x00, /* 13-14 gz = 0     */
        0x00,       /* 15 checksum: aşağıda hesaplanıp kontrol ediliyor */
    };
    uint8_t chk = 0;
    for (int i = 1; i <= 14; i++) {
        chk ^= expected[i];
    }
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, buf, 15);
    TEST_ASSERT_EQUAL_HEX8(chk, buf[15]);
}

static void test_negative_values_bytes(void)
{
    log_frame_t f = {0};
    f.accel[0] = -2; /* 0xFFFE */
    f.gyro[2] = -300; /* 0xFED4 */
    uint8_t buf[PD_FRAME_LEN];
    frame_pack(&f, buf);

    TEST_ASSERT_EQUAL_HEX8(0xFE, buf[3]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, buf[4]);
    TEST_ASSERT_EQUAL_HEX8(0xD4, buf[13]);
    TEST_ASSERT_EQUAL_HEX8(0xFE, buf[14]);
}

/* Byte 1..15'teki her bitin tek tek bozulması checksum hatası vermeli */
static void test_any_single_bit_flip_is_detected(void)
{
    log_frame_t in = sample_frame(), out;
    uint8_t buf[PD_FRAME_LEN];
    frame_pack(&in, buf);

    for (int i = 1; i < PD_FRAME_LEN; i++) {
        for (int b = 0; b < 8; b++) {
            buf[i] ^= (uint8_t)(1u << b);
            TEST_ASSERT_EQUAL(FRAME_BAD_CHECKSUM, frame_unpack(buf, &out));
            buf[i] ^= (uint8_t)(1u << b);
        }
    }
    TEST_ASSERT_EQUAL(FRAME_OK, frame_unpack(buf, &out));
}

static void test_bad_sync_rejected_and_output_untouched(void)
{
    log_frame_t in = sample_frame();
    log_frame_t out = {.seq = 99};
    uint8_t buf[PD_FRAME_LEN];
    frame_pack(&in, buf);
    buf[0] = 0x5A;

    TEST_ASSERT_EQUAL(FRAME_BAD_SYNC, frame_unpack(buf, &out));
    TEST_ASSERT_EQUAL_UINT8(99, out.seq); /* dokunulmamış */
}

static void test_reserved_flag_bits_cleared(void)
{
    log_frame_t f = sample_frame();
    f.flags = 0xFF;
    uint8_t buf[PD_FRAME_LEN];
    frame_pack(&f, buf);
    TEST_ASSERT_EQUAL_HEX8(0x1F, buf[2]);
}

static void test_frame_from_raw_drops_temperature(void)
{
    imu_raw_t raw = {.accel = {1, 2, 3}, .gyro = {-4, -5, -6}, .temp = 1234};
    log_frame_t f;
    frame_from_raw(&raw, 200, PD_FLAG_SATURATED, &f);

    TEST_ASSERT_EQUAL_UINT8(200, f.seq);
    TEST_ASSERT_EQUAL_UINT8(PD_FLAG_SATURATED, f.flags);
    TEST_ASSERT_EQUAL_INT16_ARRAY(raw.accel, f.accel, 3);
    TEST_ASSERT_EQUAL_INT16_ARRAY(raw.gyro, f.gyro, 3);
}

/*
 * Adım 4 (pdproto.py / decode.py) için örnek dosya üretir:
 * build_host/frames_test.bin  ->  300 çerçeve, seq 0..255 ve tekrar 0..43
 * Değerler: ax = i, ay = -i, az = 4096, gx = i*10, gy = -i*10, gz = 0
 *           her 50. çerçevede buton işareti.
 */
static void test_write_sample_bin_file(void)
{
    FILE *fp = fopen("frames_test.bin", "wb");
    TEST_ASSERT_NOT_NULL(fp);

    for (int i = 0; i < 300; i++) {
        log_frame_t f = {
            .seq = (uint8_t)i,
            .flags = (i % 50 == 0) ? PD_FLAG_BUTTON : 0,
            .accel = {(int16_t)i, (int16_t)-i, 4096},
            .gyro = {(int16_t)(i * 10), (int16_t)(-i * 10), 0},
        };
        uint8_t buf[PD_FRAME_LEN];
        frame_pack(&f, buf);
        TEST_ASSERT_EQUAL_size_t(PD_FRAME_LEN, fwrite(buf, 1, PD_FRAME_LEN, fp));
    }
    fclose(fp);
}

/* ================== METİN SATIRLARI ================== */

static char line[PD_LINE_MAX + 1];

static void test_line_boot(void)
{
    TEST_ASSERT_EQUAL_INT(24, line_boot(line, sizeof line, "0.1.0", "POWERON", 0x70));
    TEST_ASSERT_EQUAL_STRING("BOOT,0.1.0,POWERON,0x70\n", line);

    line_boot(line, sizeof line, "0.1.0", "TASK_WDT", -1);
    TEST_ASSERT_EQUAL_STRING("BOOT,0.1.0,TASK_WDT,NONE\n", line);

    line_boot(line, sizeof line, "0.1.0", "SW", 0x0A);
    TEST_ASSERT_EQUAL_STRING("BOOT,0.1.0,SW,0x0A\n", line);
}

static void test_line_evt_examples_from_protocol(void)
{
    gesture_result_t r = {.gesture = GESTURE_FIRE, .peak_mg = 2310, .margin_pct = 54, .t = 9643};
    line_evt(line, sizeof line, &r);
    TEST_ASSERT_EQUAL_STRING("EVT,FIRE,2310,48215,54\n", line);

    r = (gesture_result_t){.gesture = GESTURE_WEB, .peak_mg = 620, .margin_pct = 12, .t = 10268};
    line_evt(line, sizeof line, &r);
    TEST_ASSERT_EQUAL_STRING("EVT,WEB,620,51340,12\n", line);

    r = (gesture_result_t){.gesture = GESTURE_ICE, .peak_mg = 1400, .margin_pct = 0, .t = 0};
    line_evt(line, sizeof line, &r);
    TEST_ASSERT_EQUAL_STRING("EVT,ICE,1400,0,0\n", line);
}

static void test_line_evt_rejects_none(void)
{
    gesture_result_t r = {.gesture = GESTURE_NONE};
    TEST_ASSERT_EQUAL_INT(-1, line_evt(line, sizeof line, &r));
}

static void test_line_evt_large_time(void)
{
    /* 0x33333333 tick * 5 = 0xFFFFFFFF ms: uint32 sınırı, taşma yok */
    gesture_result_t r = {.gesture = GESTURE_ICE, .peak_mg = 1, .t = 0x33333333u};
    line_evt(line, sizeof line, &r);
    TEST_ASSERT_EQUAL_STRING("EVT,ICE,1,4294967295,0\n", line);
}

static void test_line_cfg_all_levels(void)
{
    line_cfg(line, sizeof line, SENS_LOW);
    TEST_ASSERT_EQUAL_STRING("CFG,LOW\n", line);
    line_cfg(line, sizeof line, SENS_MED);
    TEST_ASSERT_EQUAL_STRING("CFG,MED\n", line);
    line_cfg(line, sizeof line, SENS_HIGH);
    TEST_ASSERT_EQUAL_STRING("CFG,HIGH\n", line);
    TEST_ASSERT_EQUAL_INT(-1, line_cfg(line, sizeof line, (sensitivity_t)7));
}

static void test_line_cal(void)
{
    const int16_t bias[3] = {-34, 12, 7};
    line_cal(line, sizeof line, true, bias);
    TEST_ASSERT_EQUAL_STRING("CAL,OK,-34,12,7\n", line);

    const int16_t extreme[3] = {-32768, 32767, 0};
    line_cal(line, sizeof line, false, extreme);
    TEST_ASSERT_EQUAL_STRING("CAL,FAIL,-32768,32767,0\n", line);
}

static void test_line_err_all_faults(void)
{
    line_err(line, sizeof line, FAULT_I2C);
    TEST_ASSERT_EQUAL_STRING("ERR,I2C\n", line);
    line_err(line, sizeof line, FAULT_WHO_AM_I);
    TEST_ASSERT_EQUAL_STRING("ERR,WHOAMI\n", line);
    line_err(line, sizeof line, FAULT_SENSOR_STUCK);
    TEST_ASSERT_EQUAL_STRING("ERR,STUCK\n", line);
    line_err(line, sizeof line, FAULT_LOOP_OVERRUN);
    TEST_ASSERT_EQUAL_STRING("ERR,OVERRUN\n", line);
    line_err(line, sizeof line, FAULT_NVS);
    TEST_ASSERT_EQUAL_STRING("ERR,NVS\n", line);
    TEST_ASSERT_EQUAL_INT(-1, line_err(line, sizeof line, FAULT_NONE));
}

static void test_line_stat(void)
{
    line_stat(line, sizeof line, 612, 0, 0);
    TEST_ASSERT_EQUAL_STRING("STAT,612,0,0\n", line);
    line_stat(line, sizeof line, 4294967295u, 4294967295u, 4294967295u);
    TEST_ASSERT_EQUAL_STRING("STAT,4294967295,4294967295,4294967295\n", line);
}

static void test_too_small_buffer_returns_minus_one(void)
{
    char small[6]; /* "CFG,MED\n" 8 karakter + NUL, sığmaz */
    TEST_ASSERT_EQUAL_INT(-1, line_cfg(small, sizeof small, SENS_MED));
}

static void test_line_longer_than_max_returns_minus_one(void)
{
    char big[200];
    const char *long_fw = "0123456789012345678901234567890123456789012345678901234567890";
    TEST_ASSERT_EQUAL_INT(-1, line_boot(big, sizeof big, long_fw, "POWERON", 0x70));
}

static void test_return_value_is_length(void)
{
    int n = line_cfg(line, sizeof line, SENS_HIGH);
    TEST_ASSERT_EQUAL_INT((int)strlen(line), n);
    TEST_ASSERT_EQUAL_CHAR('\n', line[n - 1]);
}

int main(void)
{
    UNITY_BEGIN();
    /* çerçeve */
    RUN_TEST(test_pack_unpack_roundtrip);
    RUN_TEST(test_exact_byte_layout);
    RUN_TEST(test_negative_values_bytes);
    RUN_TEST(test_any_single_bit_flip_is_detected);
    RUN_TEST(test_bad_sync_rejected_and_output_untouched);
    RUN_TEST(test_reserved_flag_bits_cleared);
    RUN_TEST(test_frame_from_raw_drops_temperature);
    RUN_TEST(test_write_sample_bin_file);
    /* metin satırları */
    RUN_TEST(test_line_boot);
    RUN_TEST(test_line_evt_examples_from_protocol);
    RUN_TEST(test_line_evt_rejects_none);
    RUN_TEST(test_line_evt_large_time);
    RUN_TEST(test_line_cfg_all_levels);
    RUN_TEST(test_line_cal);
    RUN_TEST(test_line_err_all_faults);
    RUN_TEST(test_line_stat);
    RUN_TEST(test_too_small_buffer_returns_minus_one);
    RUN_TEST(test_line_longer_than_max_returns_minus_one);
    RUN_TEST(test_return_value_is_length);
    return UNITY_END();
}
