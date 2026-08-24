#pragma once

#include <stdint.h>

#define CONFIG_PACKET_SIZE 9

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


/****** mercury modes ******/
#define HERMES_MODE_MAX 10 // 0 to 10, size 11

// mercury payload_bytes_per_modem_frame for modes 0 to 10.
// These are the indices Mercury's own "-l" reports, in its order; keep this
// table in step with it (mercury -l prints "Mode index" and
// "payload_bytes_per_modem_frame" for each).
// Mode  0: DATAC1   -  510 bytes
// Mode  1: DATAC3   -  126 bytes
// Mode  2: DATAC0   -   14 bytes
// Mode  3: DATAC4   -   54 bytes
// Mode  4: DATAC13  -   14 bytes
// Mode  5: DATAC14  -    3 bytes
// Mode  6: FSK_LDPC -   30 bytes
// Mode  7: DATAC15  -   30 bytes
// Mode  8: DATAC16  -   14 bytes
// Mode  9: DATAC17  - 1180 bytes
// Mode 10: QAM16C2  - 1213 bytes
uint32_t hermes_frame_size[11] = { 510, 126, 14, 54, 14, 3, 30, 30, 14, 1180, 1213 };
