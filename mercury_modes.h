#pragma once

#include <stdint.h>

#define SHM_PAYLOAD_BUFFER_SIZE 131072
#define CONFIG_PACKET_SIZE 9
#define SHM_PAYLOAD_NAME "/mercury-comm"

#define TAG_SIZE 3
#define HERMES_SIZE 1
#define RQ_HEADER_SIZE 4 // 3 + 1

#define PACKET_TYPE_BITS 3
#define PACKET_TYPE_SHIFT 5
#define PACKET_TYPE_MASK 0x07
#define FRAME_EXT_MASK 0x1f


/*
 * These are packet types encoded in the first byte of the modem frame.
 * They are not KISS command bytes.
 *
 * In the current TCP path, the outer KISS frame normally uses CMD_DATA and the
 * receiver inspects frame[0] to distinguish RaptorQ configuration/control from
 * RaptorQ payload-bearing broadcast frames.
 */
#define PACKET_RAW 0x00
#define PACKET_UUCP 0x01
#define PACKET_RQ_CONFIG 0x03
#define PACKET_RQ_PAYLOAD 0x04

static inline uint8_t hermes_frame_packet_type(uint8_t header)
{
    return (uint8_t)((header >> PACKET_TYPE_SHIFT) & PACKET_TYPE_MASK);
}

static inline uint8_t hermes_frame_extension(uint8_t header)
{
    return (uint8_t)(header & FRAME_EXT_MASK);
}

static inline void hermes_write_frame_header(uint8_t *frame, uint8_t packet_type, uint8_t extension)
{
    if (!frame)
        return;

    frame[0] = (uint8_t)(((packet_type & PACKET_TYPE_MASK) << PACKET_TYPE_SHIFT) |
                         (extension & FRAME_EXT_MASK));
}


/****** Mercury modem modes (legacy) ******/
#define MERCURY_MODE_MAX 16 // 0 to 16, size 17

// Mercury frame sizes, no CRC enabled, modes 0 to 16.
uint32_t mercury_frame_size[17] = { 12, 25, 37, 50, 62, 75, 100, 62, 75, 100, 74, 99, 175, 100, 174, 175, 162};


/****** hermes-modem modes ******/
#define HERMES_MODE_MAX 6 // 0 to 6, size 7

// hermes-modem payload_bytes_per_modem_frame for modes 0 to 6
// Mode 0: DATAC1  - 510 bytes
// Mode 1: DATAC3  - 126 bytes
// Mode 2: DATAC0  -  14 bytes
// Mode 3: DATAC4  -  54 bytes
// Mode 4: DATAC13 -  14 bytes
// Mode 5: DATAC14 -   3 bytes
// Mode 6: FSK_LDPC - 30 bytes
uint32_t hermes_frame_size[7] = { 510, 126, 14, 54, 14, 3, 30 };
