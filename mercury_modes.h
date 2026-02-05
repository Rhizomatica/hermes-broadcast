#pragma once

#include <stdint.h>

#define SHM_PAYLOAD_BUFFER_SIZE 131072
#define CONFIG_PACKET_SIZE 9
#define SHM_PAYLOAD_NAME "/mercury-comm"

#define TAG_SIZE 3
#define HERMES_SIZE 1
#define RQ_HEADER_SIZE 4 // 3 + 1


#define PACKET_RAW 0x00
#define PACKET_UUCP 0x01
#define PACKET_RQ_CONFIG 0x02
#define PACKET_RQ_PAYLOAD 0x03


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
