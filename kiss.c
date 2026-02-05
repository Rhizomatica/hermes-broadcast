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

void kiss_init(kiss_state_t *state)
{
    state->frame_len = 0;
    state->in_frame = 0;
    state->escape = 0;
    state->kiss_command = CMD_UNKNOWN;
}

int kiss_read(kiss_state_t *state, uint8_t sbyte, uint8_t *frame_buffer)
{
    if (state->in_frame && sbyte == FEND && state->kiss_command == CMD_DATA)
    {
        state->in_frame = 0;
        return state->frame_len;
    }
    if (sbyte == FEND)
    {
        state->in_frame = 1;
        state->kiss_command = CMD_UNKNOWN;
        state->frame_len = 0;
        return 0;
    }
    if (state->in_frame && state->frame_len < MAX_PAYLOAD)
    {
        // Have a look at the command byte first
        if (state->frame_len == 0 && state->kiss_command == CMD_UNKNOWN)
        {
            // Strip of port nibble
            state->kiss_command = sbyte & 0x0F;
            return 0;
        }
        if (state->kiss_command == CMD_DATA)
        {
            if (sbyte == FESC)
            {
                state->escape = 1;
                return 0;
            }
            if (state->escape)
            {
                if (sbyte == TFEND) sbyte = FEND;
                if (sbyte == TFESC) sbyte = FESC;
                state->escape = 0;
            }
            if (state->frame_len < MAX_PAYLOAD)
            {
                frame_buffer[state->frame_len++] = sbyte;
            }
        }
    }
    return 0;
}

int kiss_write_frame(uint8_t *buffer, int frame_len, uint8_t *write_buffer)
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
    return write_len;
}
