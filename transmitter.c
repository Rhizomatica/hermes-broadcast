/* RaptorQ fountain code transmitter
 *
 * Copyright (C) 2020-2024 Rhizomatica
 * Author: Rafael Diniz <rafael@rhizomatica.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>

#include "ring_buffer_posix.h"
#include "mercury_modes.h"
#include "crc6.h"

#include <nanorq.h>


#define MAX_ESI 65535

bool running;

uint8_t configuration_packet[CONFIG_PACKET_SIZE];

void exit_system(int sig)
{
    printf("Exiting... ");
    running = false;
}

void write_esi(nanorq *rq, struct ioctx *myio, uint8_t sbn,
              uint32_t esi, cbuf_handle_t buffer)
{
    size_t packet_size = nanorq_symbol_size(rq);
    uint8_t data[packet_size + RQ_HEADER_SIZE];
    memset(data, 0, packet_size);
    uint64_t written = nanorq_encode(rq, (void *)data, esi, sbn, myio);

    if (written != packet_size)
    {
        fprintf(stdout, "failed to encode packet data for sbn %d esi %d.", sbn, esi);
        abort();
    }
    else
    {
        memmove (data + RQ_HEADER_SIZE, data, packet_size);
        // add our reduced tag
        nanorq_tag_reduced(sbn, esi, data+1); // 3 bytes

        // set payload packet type
        data[0] = (PACKET_RQ_PAYLOAD << 6) & 0xff;
        data[0] |= crc6_0X6F(1, data+1, packet_size + TAG_SIZE);

        write_buffer(buffer, data, packet_size + RQ_HEADER_SIZE);
        fprintf(stdout, "\rBlock: %2d  Tx: %3d",  sbn, esi);
        fflush(stdout);
        // for (int i = 0; i < packet_size + RQ_HEADER_SIZE; i++)
        //    printf("%02x ", data[i]);
        // printf("\n");
    }
}

bool write_interleaved_block_packets(nanorq *rq, struct ioctx *myio, uint32_t *esi, cbuf_handle_t buffer)
{
    int num_sbn = nanorq_blocks(rq);

    // for all blocks TODO: shuffle the sbn traversal each call
    for (int sbn = 0; sbn < num_sbn; sbn++)
    {
        write_esi(rq, myio, sbn, esi[sbn], buffer);
        esi[sbn]++;
//        if (esi[sbn] > ((1 << 24) - 1))
        if (esi[sbn] > ((1 << 16) - 1))
        {
            // printf("ESI LIMIT REACHED, PLEASE INCREASE-ME BACK TO 24 BITS!\n");
            return false;
            // esi[sbn] = 0;
        }
    }
    return true;
}

void write_configuration_packet(int packet_size, cbuf_handle_t buffer)
{
    uint8_t data[packet_size + RQ_HEADER_SIZE];

    memset(data, 0, packet_size);

    write_buffer(buffer, configuration_packet, CONFIG_PACKET_SIZE);
    // stuffing bytes... could be used for something useful later on
    if (packet_size > CONFIG_PACKET_SIZE)
        write_buffer(buffer, data, packet_size - CONFIG_PACKET_SIZE);
}


int main(int argc, char *argv[]) {

    if (argc < 3)
    {
        printf("Usage: %s file_to_transmit mercury_modulation_mode\n", argv[0]);
        printf("mercury_modulation_mode ranges from 0 to 16 (inclusive)\n");
        return -1;
    }
    char *infile = argv[1];
    struct ioctx *myio = ioctx_from_file(infile, 1);
    if (!myio)
    {
        fprintf(stdout, "couldnt access file %s\n", infile);
        return -1;
    }

    size_t filesize = myio->size(myio);

    // (2 ^ 24) - 1 is the maximum size we support for now (a.k.a. 16 MB)
    if (filesize > 16777215)
    {
        printf("File to transmit exceed 16MB. Input size %lu: Max size: %d\n", filesize, 16777215);
        exit(-1);
    }

    int mod_mode = strtol(argv[2], NULL, 10);
    size_t packet_size = 0;

    // determine chunks, symbol size, memory usage from size
    if (mod_mode <= 16)
    {
        packet_size = mercury_frame_size[mod_mode] - (uint32_t) RQ_HEADER_SIZE; // T
    }
    else
    {
        printf("Invalid mode. Valid modes range from 0 to 16 (inclusive).\n");
        exit(-1);
    }

    running = true;
    signal(SIGQUIT, exit_system);
    signal(SIGINT, exit_system);
    signal(SIGTERM, exit_system);

    uint8_t align = 1;

    srand((unsigned int)time(0));

    nanorq *rq = nanorq_encoder_new(filesize, packet_size, align);

    if (rq == NULL)
    {
        fprintf(stdout, "Could not initialize encoder.\n");
        return -1;
    }

    // 16 bits for esi
    nanorq_set_max_esi(rq, MAX_ESI);

    int num_sbn = nanorq_blocks(rq);
    packet_size = nanorq_symbol_size(rq);
    uint32_t esi[num_sbn];

    memset(esi, 0, num_sbn * sizeof(uint32_t));

    printf("\e[?25l"); // hide cursor
    printf("RaptorQ initialized: sbn (blocks) = %d  Packet_size: %lu\n", num_sbn, packet_size);

    for (int b = 0; b < num_sbn; b++)
    {
        nanorq_generate_symbols(rq, b, myio);
    }

    memset(configuration_packet, 0, CONFIG_PACKET_SIZE);

    nanorq_oti_common_reduced(rq, configuration_packet+1); // 5 bytes
    nanorq_oti_scheme_specific_align1(rq, configuration_packet+6); // 3 bytes

    configuration_packet[0] = (PACKET_RQ_CONFIG << 6) & 0xff;
    configuration_packet[0] |= crc6_0X6F(1, configuration_packet + HERMES_SIZE, CONFIG_PACKET_SIZE - HERMES_SIZE);

    cbuf_handle_t buffer;

    buffer = circular_buf_connect_shm(SHM_PAYLOAD_BUFFER_SIZE, SHM_PAYLOAD_NAME);

    while(running)
    {
        // 1 configuration packet per each sbn "slice"
        write_configuration_packet(mercury_frame_size[mod_mode], buffer);

        if (write_interleaved_block_packets(rq, myio, esi, buffer) == false)
            running = false;
    }

    printf("shutdown.\n");
    printf("\e[?25h"); // re-enable cursor

    nanorq_free(rq);
    myio->destroy(myio);

    circular_buf_free_shm(buffer);

    return 0;
}
