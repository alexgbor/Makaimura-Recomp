/**
 *    Name:        inflate.h
 *    Description: DEFLATE (RFC 1951) decompression and CRC-32, for the .zip files of the player's ROM set
 *    Version:     0.1.0
 *    Created:     19-11-2026
 *
 *    license:BSD-3-Clause
 */
#ifndef GNG_INFLATE_H
#define GNG_INFLATE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The raw DEFLATE stream 'in' into 'out': the bytes it gave, or -1 if it is broken or does not fit */
long gng_inflate(const uint8_t *in, size_t in_size, uint8_t *out, size_t out_size);

/* The CRC-32 of zip (ISO-HDLC: reflected 0x04C11DB7, from and to all ones) */
uint32_t gng_crc32(const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif
