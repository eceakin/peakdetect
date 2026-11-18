#include <string.h>

#include "crc16.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* Standart kontrol değeri: bu geçiyorsa algoritma doğru */
static void test_check_value_123456789(void)
{
    const char *s = "123456789";
    TEST_ASSERT_EQUAL_HEX16(0x29B1, crc16((const uint8_t *)s, strlen(s)));
}

/* Boş girdi: hiçbir byte işlenmez, başlangıç değeri döner */
static void test_empty_input_returns_init(void)
{
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, crc16(NULL, 0));
}

static void test_single_byte_A(void)
{
    const uint8_t a = 'A';
    TEST_ASSERT_EQUAL_HEX16(0xB915, crc16(&a, 1));
}

/* Parça parça hesap, tek seferlik hesapla aynı olmalı (NVS'te alan alan hesap için) */
static void test_incremental_equals_one_shot(void)
{
    const uint8_t *s = (const uint8_t *)"123456789";
    uint16_t crc = crc16_update(CRC16_INIT, s, 4);
    crc = crc16_update(crc, s + 4, 5);
    TEST_ASSERT_EQUAL_HEX16(0x29B1, crc);
}

/* Tek bit bozulunca CRC değişmeli: bozuk ayar kaydı böyle yakalanır */
static void test_single_bit_flip_changes_crc(void)
{
    uint8_t buf[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint16_t good = crc16(buf, sizeof buf);
    for (size_t i = 0; i < sizeof buf; i++) {
        for (int b = 0; b < 8; b++) {
            buf[i] ^= (uint8_t)(1u << b);
            TEST_ASSERT_NOT_EQUAL(good, crc16(buf, sizeof buf));
            buf[i] ^= (uint8_t)(1u << b); /* geri al */
        }
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_check_value_123456789);
    RUN_TEST(test_empty_input_returns_init);
    RUN_TEST(test_single_byte_A);
    RUN_TEST(test_incremental_equals_one_shot);
    RUN_TEST(test_single_bit_flip_changes_crc);
    return UNITY_END();
}
