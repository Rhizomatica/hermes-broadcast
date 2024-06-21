#include <stdint.h>

#ifndef KISS_H_
#define KISS_H_

#define FEND 0xC0
#define FESC 0xDB
#define TFEND 0xDC
#define TFESC 0xDD

#define CMD_UNKNOWN 0xFE
#define CMD_AX25 0x00 //  AX25 Frame (standard) in VARA
#define CMD_AX25CALLSIGN 0x01 // AX25 Frame (7 chrs Call Signs) in VARA
#define CMD_DATA 0x02 // VARA unformatted framte

#define MAX_PAYLOAD 756 // ~ 18 frames at VARA Level 4

void kiss_read(uint8_t sbyte);
int kiss_write_frame(uint8_t* buffer, int frame_len);


#endif // KISS_H_
