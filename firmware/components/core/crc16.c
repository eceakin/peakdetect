#include "crc16.h"

#define CRC16_POLY 0x1021u

uint16_t crc16_update(uint16_t crc, const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)((uint16_t)data[i] << 8); /* byte'ı üst 8 bite koy */
        for (int bit = 0; bit < 8; bit++) {
            if (crc & 0x8000u) {
                crc = (uint16_t)((crc << 1) ^ CRC16_POLY);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}

uint16_t crc16(const uint8_t *data, size_t len)
{
    return crc16_update(CRC16_INIT, data, len);
}
