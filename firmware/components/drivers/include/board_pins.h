/*
 * board_pins.h - PeakDetect donanım bağlantıları
 *
 * Kaynak: proje önerisi Bölüm 6 (Hardware Components).
 * Sadece drivers katmanı kullanır. Kablolama değişirse yalnızca burası değişir.
 *
 * Kaçınılan pinler: GPIO6-11 (flash), GPIO34-39 (sadece giriş, pull-up yok),
 * GPIO0/5/12/15 (strapping). GPIO2 strapping'dir ama yalnızca açılıştan sonra çıkış.
 */
#ifndef BOARD_PINS_H
#define BOARD_PINS_H

/* ---------------- I2C: MPU-6500 ---------------- */
#define PD_I2C_PORT 0
#define PD_PIN_I2C_SDA 21
#define PD_PIN_I2C_SCL 22
#define PD_MPU_ADDR 0x68 /* AD0 = GND */

/* ---------------- LED'ler (aktif yüksek) ---------------- */
#define PD_PIN_LED_WEB 25  /* yeşil, 220 Ω */
#define PD_PIN_LED_ICE 26  /* mavi, 100 Ω  */
#define PD_PIN_LED_FIRE 27 /* kırmızı, 220 Ω */
#define PD_PIN_LED_ERR 2   /* kart üstü LED */
#define PD_LED_ON_LEVEL 1

/* ---------------- Buzzer (LEDC PWM) ---------------- */
#define PD_PIN_BUZZER 18       /* 100 Ω seri direnç */
#define PD_LEDC_TIMER 0
#define PD_LEDC_CHANNEL 0
#define PD_LEDC_DUTY_RES_BITS 10
#define PD_LEDC_DUTY_50 512    /* %50 = 2^10 / 2 */

/* ---------------- Buton (aktif düşük, dahili pull-up) ---------------- */
#define PD_PIN_BUTTON 4
#define PD_BUTTON_PRESSED_LEVEL 0

/* ---------------- Debug pinleri (mantık analizörü) ---------------- */
#define PD_PIN_DEBUG_TICK 23  /* her 5 ms kesmede değişir -> 100 Hz kare dalga */
#define PD_PIN_DEBUG_ARMED 19 /* hareket penceresi açılınca 1 (gecikme ölçümü) */

/* ---------------- UART ---------------- */
#define PD_UART_PORT 0 /* USB köprüsü: TX = GPIO1, RX = GPIO3 */

#endif /* BOARD_PINS_H */
