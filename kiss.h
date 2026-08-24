/* KISS framer
 *
 * Copyright (C) 2020-2024 Rhizomatica
 * Author: Rafael Diniz <rafael@rhizomatica.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FEND 0xC0
#define FESC 0xDB
#define TFEND 0xDC
#define TFESC 0xDD

/*
 * KISS command bytes describe the outer TCP/TNC framing only.
 *
 * Current hermes-broadcast TCP interoperability with Mercury sends raw modem
 * frames inside CMD_DATA. The first byte of that modem frame then carries the
 * Mercury/hermes broadcast packet type defined in mercury_modes.h.
 *
 * So PACKET_RQ_CONFIG / PACKET_RQ_PAYLOAD live in frame[0], while CMD_RQ_* are
 * only reserved/legacy KISS command-space values and are not the normal wire
 * format used by transmitter/receiver/broadcast_daemon over TCP.
 */
#define CMD_UNKNOWN 0xFE
#define CMD_AX25 0x00 //  AX25 Frame (standard) in VARA
#define CMD_AX25CALLSIGN 0x01 // AX25 Frame (7 chrs Call Signs) in VARA
#define CMD_DATA 0x02 // Raw/unformatted KISS payload; current Mercury TCP framing uses this
#define CMD_RQ_CONFIG 0x03 // Reserved/legacy KISS command value; current TCP framing does not use it for RaptorQ config
#define CMD_RQ_PAYLOAD 0x04 // Reserved/legacy KISS command value; current TCP framing does not use it for RaptorQ payload

#define MAX_PAYLOAD 1213 // largest Mercury broadcast frame (QAM16C2, mode 10)

// KISS state structure for reentrant/thread-safe operation
typedef struct {
    int frame_len;
    int in_frame;
    int escape;
    uint8_t kiss_command;
} kiss_state_t;

// Initialize KISS state
void kiss_init(kiss_state_t *state);

// Process a single byte, returns frame length when complete frame received, 0 otherwise
int kiss_read(kiss_state_t *state, uint8_t sbyte, uint8_t *frame_buffer);

// Write a KISS frame to write_buffer, returns the total length written
int kiss_write_frame(uint8_t *buffer, int frame_len, uint8_t *write_buffer);


#ifdef __cplusplus
};
#endif
