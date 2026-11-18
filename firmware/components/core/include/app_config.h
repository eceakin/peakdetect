/*
 * app_config.h - PeakDetect sayıları tek yerde
 *
 * Kodun içinde sayı yazılmaz; süreler, eşikler ve limitler buradan gelir.
 * Süreler hem ms hem tick (5 ms) olarak verilir. Her süre 5 ms'nin katı olmalı;
 * aşağıdaki kontroller değilse derlemeyi durdurur.
 *
 * Bu dosya core içindedir çünkü PC testleri de bu sayıları kullanır.
 * Pin numaraları burada DEĞİL, drivers/include/board_pins.h içindedir.
 */
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* ---------------- Sürüm ---------------- */
#define PD_FW_VERSION "0.1.0"
#define PD_SETTINGS_VERSION 1 /* settings_t düzeni değişirse artırın */

/* ---------------- Zaman tabanı ---------------- */
#define PD_TICK_MS 5
#define PD_SAMPLE_HZ (1000 / PD_TICK_MS) /* 200 */
#define PD_MS_TO_TICKS(ms) ((ms) / PD_TICK_MS)

#define PD_WINDOW_MS 60
#define PD_WINDOW_SAMPLES PD_MS_TO_TICKS(PD_WINDOW_MS) /* 12 */

#define PD_COOLDOWN_MS 500
#define PD_COOLDOWN_TICKS PD_MS_TO_TICKS(PD_COOLDOWN_MS) /* 100 */

#define PD_ABILITY_LED_MS 300
#define PD_ABILITY_LED_TICKS PD_MS_TO_TICKS(PD_ABILITY_LED_MS) /* 60 */

#define PD_DEBOUNCE_SAMPLES 6 /* 6 x 5 ms = 30 ms aynı seviye */
#define PD_LONG_PRESS_MS 1000
#define PD_LONG_PRESS_TICKS PD_MS_TO_TICKS(PD_LONG_PRESS_MS) /* 200 */

#define PD_LOOP_TIMEOUT_MS 20  /* tik gelmezse loop-overrun hatası */
#define PD_WDT_TIMEOUT_MS 500  /* görev watchdog'u */

/* ---------------- Sensör ölçekleri ---------------- */
#define PD_ACCEL_LSB_PER_G 4096 /* ±8 g */
#define PD_ACCEL_MG_NUM 125     /* mg = raw * 125 / 512 */
#define PD_ACCEL_MG_DEN 512
#define PD_GYRO_DPS10_NUM 20    /* 0.1 dps = raw * 20 / 131 */
#define PD_GYRO_DPS10_DEN 131
#define PD_SAT_LIMIT 32767      /* |raw| >= bu ise doymuş */
#define PD_SCALE_Q10_ONE 1024   /* accel_scale = 1.0 */

/* ---------------- Kalibrasyon ---------------- */
#define PD_CALIB_SAMPLES 200        /* 1 s hareketsiz */
#define PD_CALIB_MAX_SPREAD_LSB 130 /* bir eksende max-min > ~2 °/s ise FAIL (el kıpırdadı) */
#define PD_GYRO_BIAS_LIMIT_LSB 1310 /* |bias| > ~20 °/s ise kayıt geçersiz */
#define PD_ACCEL_SCALE_MIN_Q10 973  /* 0.95 */
#define PD_ACCEL_SCALE_MAX_Q10 1075 /* 1.05 */

/* ---------------- Yerçekimi filtresi ---------------- */
#define PD_GRAVITY_SHIFT 6         /* g_est += (a - g_est) >> 6, ~0.3 s */
#define PD_GRAVITY_FREEZE_W 1500   /* 0.1 dps, |wx|+|wy|+|wz| > 150 °/s ise dondur */

/* ---------------- Eşikler (Medium, başlangıç tahmini; Adım 8'de veriyle güncellenir) ---------------- */
#define PD_TH_TRIGGER_MG 800  /* mg, ARMED için ivme */
#define PD_TH_TRIGGER_W 2500  /* 0.1 dps, ARMED için dönüş (250 °/s) */
#define PD_TH_WEB_W_MIN 3500  /* 0.1 dps, 350 °/s */
#define PD_TH_WEB_A_MAX 1500  /* mg, 1.5 g */
#define PD_TH_ICE_A_MIN 1200  /* mg, 1.2 g */
#define PD_TH_FIRE_A_MIN 1500 /* mg, 1.5 g */
#define PD_TH_DOMINANCE_X10 15 /* 1.5 kat baskınlık */
#define PD_REARM_PCT 70        /* cooldown sonrası sinyal tetik eşiğinin %70'inin altına inmeden ARMED olmaz */
#define PD_AMBIGUITY_GAP_PCT 20 /* iki kural birden geçerse: margin farkı >= bu ise büyük olan kazanır, değilse AMBIGUOUS */

/* Hassasiyet çarpanları, yüzde */
#define PD_SENS_LOW_PCT 130
#define PD_SENS_MED_PCT 100
#define PD_SENS_HIGH_PCT 75
#define PD_SENS_DEFAULT 1 /* SENS_MED */

/* ---------------- Sensör ve haberleşme ---------------- */
#define PD_WHO_AM_I_EXPECTED 0x70 /* bring-up'ta (Adım 5.5) okunan değerle güncellenir */
#define PD_I2C_FREQ_HZ 400000
#define PD_I2C_TIMEOUT_MS 2
#define PD_I2C_RETRIES 3
#define PD_I2C_ERRORS_TO_RECOVER 3 /* art arda bu kadar hata -> bus reset */
#define PD_STUCK_SAMPLES 200       /* 1 s birebir aynı örnek -> sensör takıldı */

#define PD_UART_BAUD 115200
#define PD_UART_TX_BUF 1024
#define PD_LINE_MAX 64 /* bir metin satırının en fazla uzunluğu, \n dahil */

#define PD_FRAME_SYNC 0xA5
#define PD_FRAME_LEN 16

#define PD_NET_QUEUE_LEN 8 /* opsiyonel Wi-Fi olay kuyruğu */

/* ---------------- Derleme zamanı kontrolleri ---------------- */
#if (PD_WINDOW_MS % PD_TICK_MS) || (PD_COOLDOWN_MS % PD_TICK_MS) || \
    (PD_ABILITY_LED_MS % PD_TICK_MS) || (PD_LONG_PRESS_MS % PD_TICK_MS)
#error "app_config.h: tüm süreler 5 ms'nin katı olmalı"
#endif

#if PD_FRAME_LEN != 16
#error "app_config.h: log çerçevesi 16 byte olmalı (docs/protocol.md)"
#endif

#endif /* APP_CONFIG_H */
