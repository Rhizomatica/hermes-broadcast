/* KISS framer
 *
 * Copyright (C) 2020-2024 Rhizomatica
 * Author: Rafael Diniz <rafael@rhizomatica.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#include <unistd.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include "kiss.h"

int frame_len;
bool IN_FRAME;
bool ESCAPE;

uint8_t kiss_command = CMD_UNKNOWN;
uint8_t frame_buffer[MAX_PAYLOAD];
uint8_t write_buffer[MAX_PAYLOAD*2+3];

void kiss_frame_received(int frame_len)
{
    // do something here...
    int written = write(1, frame_buffer, frame_len);
}

void kiss_read(uint8_t sbyte)
{
    if (IN_FRAME && sbyte == FEND && kiss_command == CMD_DATA)
    {
        IN_FRAME = false;
        kiss_frame_received(frame_len);
        return;
    }
    if (sbyte == FEND)
    {
        IN_FRAME = true;
        kiss_command = CMD_UNKNOWN;
        frame_len = 0;
        return;
    }
    if (IN_FRAME && frame_len < MAX_PAYLOAD)
    {
        // Have a look at the command byte first
        if (frame_len == 0 && kiss_command == CMD_UNKNOWN)
        {
            // Strip of port nibble
            kiss_command = sbyte & 0x0F;
            return;
        }
        if (kiss_command == CMD_DATA)
        {
            if (sbyte == FESC)
            {
                ESCAPE = true;
                return;
            }
            if (ESCAPE)
            {
                if (sbyte == TFEND) sbyte = FEND;
                if (sbyte == TFESC) sbyte = FESC;
                ESCAPE = false;
            }
            if (frame_len < MAX_PAYLOAD)
            {
                frame_buffer[frame_len++] = sbyte;
            }
        }
    }
}

int kiss_write_frame(uint8_t* buffer, int frame_len)
{
    int write_len = 0;
    write_buffer[write_len++] = FEND;
    write_buffer[write_len++] = CMD_DATA;
    for (int i = 0; i < frame_len; i++)
    {
        uint8_t byte = buffer[i];
        switch(byte)
        {
        case FEND:
            write_buffer[write_len++] = FESC;
            write_buffer[write_len++] = TFEND;
            break;
        case FESC:
            write_buffer[write_len++] = FESC;
            write_buffer[write_len++] = TFESC;
            break;
        default:
            write_buffer[write_len++] = byte;
        }
    }
    write_buffer[write_len++] = FEND;

    // TODO: write the buffer somewhere...
    return write(1, write_buffer, write_len);
}
