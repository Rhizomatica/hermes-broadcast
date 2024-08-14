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


#define MODE_MAX 16 // 0 to 16, size 17

// frame sizesno CRC enabled, modes 0 to 16.
uint32_t mercury_frame_size[17] = { 12, 25, 37, 50, 62, 75, 100, 62, 75, 100, 74, 99, 175, 100, 174, 175, 162};
