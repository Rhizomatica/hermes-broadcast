#pragma once


#define PACKET_RAW 0x0
#define PACKET_UUCP 0x1
#define PACKET_RQ_CONFIG 0x2
#define PACKET_RQ_PAYLOAD 0x3


#define MODE_MAX 16 // 0 to 16, size 17

// frame sizesno CRC enabled, modes 0 to 16.
int mercury_frame_size[] = { 12, 25, 37, 50, 62, 75, 100, 62, 75, 100, 74, 99, 175, 100, 174, 175, 162};
