/*
 * pd_types.h - PeakDetect ortak veri tipleri
 *
 * Bu dosya modüllerin birbirine aktardığı her şeyi tanımlar.
 * Değiştirmeden önce ekibe haber verin: her modül buna dayanır.
 *
 * Birim kuralı: her alanın yanında birimi yazar.
 *   LSB      = sensörün ham sayısı
 *   mg       = mili-g (1 g = 1000 mg)
 *   0.1 dps  = 0.1 °/s (3500 = 350 °/s)
 *   tick     = 5 ms'lik döngü adımı (200 Hz)
 *
 * Eksen kuralı (sensör elin sırtında):
 *   X = parmaklara doğru (ileri), Y = yanal (silkme ekseni), Z = elin sırtından yukarı
 *
 * Bu dosya ESP-IDF başlığı içermez; PC testleri de kullanır.
 */
#ifndef PD_TYPES_H
#define PD_TYPES_H

#include <stdbool.h>
#include <stdint.h>

/* Dizi indeksleri */
enum { AX_X = 0, AX_Y = 1, AX_Z = 2 };

/* Zaman: açılıştan beri geçen 5 ms'lik adım sayısı. ms = tick * 5 */
typedef uint32_t pd_tick_t; /* tick */

/* ------------------------------------------------------------------ */
/* Sensör verisi                                                       */
/* ------------------------------------------------------------------ */

/* MPU-6500'den okunan ham örnek (big-endian'dan çözülmüş hali) */
typedef struct {
    int16_t accel[3]; /* LSB, ±8 g aralığı: 4096 LSB = 1 g */
    int16_t gyro[3];  /* LSB, ±500 °/s aralığı: 65.5 LSB = 1 °/s */
    int16_t temp;     /* LSB, ham sıcaklık (kullanılmıyor) */
} imu_raw_t;

/* Birime çevrilmiş örnek (imu_convert çıktısı) */
typedef struct {
    int32_t a[3];   /* mg, ölçek düzeltmesi uygulanmış */
    int32_t w[3];   /* 0.1 dps, gyro bias çıkarılmış */
    bool saturated; /* herhangi bir eksen ±32767 / -32768'e dayandı */
} imu_sample_t;

/* Yerçekimi çıkarılmış hareket (gravity_filter çıktısı) */
typedef struct {
    int32_t a_lin[3];    /* mg, doğrusal ivme = a - g_est */
    int32_t a_lin_mag;   /* mg, |a_lin| vektör büyüklüğü (tamsayı karekök) */
    int32_t w[3];        /* 0.1 dps, imu_sample_t'den aynen */
    bool gravity_frozen; /* bu örnekte g_est güncellenmedi */
} motion_t;

/* 60 ms'lik pencerede toplanan tepe değerleri (gesture_detector -> classifier) */
typedef struct {
    int32_t a_pos[3]; /* mg, eksen başına en büyük pozitif a_lin */
    int32_t a_abs[3]; /* mg, eksen başına en büyük |a_lin| */
    int32_t a_mag;    /* mg, en büyük |a_lin| (vektör) */
    int32_t w_abs[3]; /* 0.1 dps, eksen başına en büyük |w| */
    uint8_t n_samples; /* toplanan örnek sayısı (tam pencere = PD_WINDOW_SAMPLES) */
    bool saturated;    /* penceredeki en az bir örnek doymuş */
} window_peaks_t;

/* ------------------------------------------------------------------ */
/* Hareket kararı                                                      */
/* ------------------------------------------------------------------ */

/* Değerler log çerçevesindeki flags bit 1:0 ile aynıdır, değiştirmeyin. */
typedef enum {
    GESTURE_NONE = 0,
    GESTURE_WEB = 1,  /* bilek silkme  -> Web Shot   */
    GESTURE_ICE = 2,  /* ani kaldırma  -> Ice Shield */
    GESTURE_FIRE = 3, /* sert itme     -> Fireball   */
} gesture_t;

typedef enum {
    REJECT_NONE = 0,     /* kabul edildi */
    REJECT_NO_RULE,      /* hiçbir kural sağlanmadı */
    REJECT_AMBIGUOUS,    /* birden fazla kural sağlandı, fark yetersiz */
    REJECT_SATURATED,    /* pencerede doymuş örnek var */
    REJECT_WEB_TOO_HARD, /* dönüş yeterli ama |a_lin| üst sınırı aşıldı */
} reject_reason_t;

typedef struct {
    gesture_t gesture;      /* GESTURE_NONE ise reject dolu */
    reject_reason_t reject;
    int32_t peak_mg;        /* mg, penceredeki en büyük |a_lin| (EVT satırına gider) */
    int16_t margin_pct;     /* %, belirleyici tepe eşiği ne kadar aştı: (tepe-eşik)*100/eşik */
    pd_tick_t t;            /* tick, kararın verildiği an */
} gesture_result_t;

/* ------------------------------------------------------------------ */
/* Eşikler ve hassasiyet                                               */
/* ------------------------------------------------------------------ */

typedef enum {
    SENS_LOW = 0,  /* eşikler x1.30 -> zor tetiklenir */
    SENS_MED = 1,  /* eşikler x1.00 */
    SENS_HIGH = 2, /* eşikler x0.75 -> kolay tetiklenir */
} sensitivity_t;

/* Hassasiyete göre ölçeklenmiş eşik seti (sensitivity modülü üretir) */
typedef struct {
    int32_t trigger_mg;    /* mg, herhangi bir eksende |a_lin| bunu aşarsa ARMED */
    int32_t trigger_w;     /* 0.1 dps, ya da herhangi bir |w| bunu aşarsa ARMED */
    int32_t web_w_min;     /* 0.1 dps, Web: tepe |w_y| >= bu */
    int32_t web_a_max;     /* mg, Web: tepe |a_lin| < bu (üst sınır) */
    int32_t ice_a_min;     /* mg, Ice: tepe a_lin,z >= bu */
    int32_t fire_a_min;    /* mg, Fire: tepe a_lin,x >= bu */
    int32_t dominance_x10; /* oran x10 (15 = 1.5); hassasiyetle ölçeklenmez */
} thresholds_t;

/* ------------------------------------------------------------------ */
/* Kalibrasyon ve kalıcı ayarlar                                       */
/* ------------------------------------------------------------------ */

typedef struct {
    int16_t gyro_bias[3];   /* LSB, 1 s hareketsiz ortalama */
    int16_t accel_scale[3]; /* Q10 çarpan, 1024 = 1.0 (düzeltme yoksa 1024) */
} calib_t;

/* NVS'te tek blob olarak saklanır. crc, kendisinden önceki byte'ların CRC-16'sıdır. */
typedef struct {
    uint8_t version;     /* PD_SETTINGS_VERSION; farklıysa varsayılana dön */
    uint8_t sensitivity; /* sensitivity_t değeri (uint8_t olarak saklanır) */
    calib_t calib;
    uint16_t crc;        /* CRC-16/CCITT-FALSE */
} settings_t;

/* ------------------------------------------------------------------ */
/* Kullanıcı arayüzü ve sistem durumu                                  */
/* ------------------------------------------------------------------ */

typedef enum {
    BTN_NONE = 0,
    BTN_SHORT,        /* bırakıldı, basılı kalma < 1 s */
    BTN_LONG,         /* 1 s basılı kalındı (bir kez üretilir) */
    BTN_HELD_AT_BOOT, /* açılışta basılıydı -> LOG modu */
} button_event_t;

/* feedback_player desenleri. Öncelik: FAULT > CAL_* > ability > SENS_* > BOOT */
typedef enum {
    PAT_NONE = 0,
    PAT_WEB,
    PAT_ICE,
    PAT_FIRE,
    PAT_SENS_LOW,
    PAT_SENS_MED,
    PAT_SENS_HIGH,
    PAT_CAL_START,
    PAT_CAL_OK,
    PAT_CAL_FAIL,
    PAT_BOOT,
    PAT_FAULT,
    PAT_COUNT /* son eleman, dizi boyutu için */
} pattern_id_t;

typedef enum {
    MODE_BOOT = 0,
    MODE_SELFTEST, /* WHO_AM_I ve ilk okuma kontrolü */
    MODE_RUN,      /* normal çalışma */
    MODE_CALIBRATE,
    MODE_LOG,      /* geliştirme: ham çerçeve akışı */
    MODE_FAULT,
} sys_mode_t;

/* ERR satırındaki adlar docs/protocol.md'de */
typedef enum {
    FAULT_NONE = 0,
    FAULT_I2C,          /* zaman aşımı / NACK, yeniden denemeler tükendi */
    FAULT_WHO_AM_I,     /* sensör kimliği beklenen değil */
    FAULT_SENSOR_STUCK, /* PD_STUCK_SAMPLES boyunca birebir aynı örnek */
    FAULT_LOOP_OVERRUN, /* tik bildirimi PD_LOOP_TIMEOUT_MS içinde gelmedi */
    FAULT_NVS,          /* ayarlar okunamadı/yazılamadı */
} fault_t;

#endif /* PD_TYPES_H */
