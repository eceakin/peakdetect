/*
 * crc16.h - CRC-16/CCITT-FALSE
 *
 * Polinom 0x1021, başlangıç 0xFFFF, yansıtma yok, son XOR yok.
 * Kontrol değeri: "123456789" -> 0x29B1
 *
 * Kullanım yeri: settings_t'nin NVS'te bozulup bozulmadığını anlamak.
 * (Log çerçeveleri CRC değil, daha basit bir XOR checksum kullanır; bkz. protocol.md)
 */
#ifndef CRC16_H
#define CRC16_H

#include <stddef.h>
#include <stdint.h>

#define CRC16_INIT 0xFFFFu

/* Parça parça hesap: crc = crc16_update(CRC16_INIT, a, n); crc = crc16_update(crc, b, m); */
uint16_t crc16_update(uint16_t crc, const uint8_t *data, size_t len);

/* Tek seferde hesap: crc16_update(CRC16_INIT, data, len) ile aynı */
uint16_t crc16(const uint8_t *data, size_t len);

#endif /* CRC16_H */
