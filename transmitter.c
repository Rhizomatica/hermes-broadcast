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
#include <getopt.h>

#include "ring_buffer_posix.h"
#include "mercury_modes.h"
#include "crc6.h"
#include "tcp_interface.h"

#include <nanorq.h>


#define MAX_ESI 65535

bool running;

uint8_t configuration_packet[CONFIG_PACKET_SIZE];

// Output mode
typedef enum {
    OUTPUT_SHM,
    OUTPUT_TCP
} output_mode_t;

// Global TCP interface (used when OUTPUT_TCP mode)
tcp_interface_t tcp_iface;

void exit_system(int sig)
{
    printf("\nExiting... ");
    running = false;
}

void write_esi(nanorq *rq, struct ioctx *myio, uint8_t sbn,
              uint32_t esi, cbuf_handle_t buffer, output_mode_t out_mode)
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

        if (out_mode == OUTPUT_SHM)
        {
            write_buffer(buffer, data, packet_size + RQ_HEADER_SIZE);
        }
        else // OUTPUT_TCP
        {
            tcp_interface_send_kiss(&tcp_iface, data, packet_size + RQ_HEADER_SIZE);
        }
        fprintf(stdout, "\rBlock: %2d  Tx: %3d",  sbn, esi);
        fflush(stdout);
        // for (int i = 0; i < packet_size + RQ_HEADER_SIZE; i++)
        //    printf("%02x ", data[i]);
        // printf("\n");
    }
}

bool write_interleaved_block_packets(nanorq *rq, struct ioctx *myio, uint32_t *esi, cbuf_handle_t buffer, output_mode_t out_mode)
{
    int num_sbn = nanorq_blocks(rq);

    // for all blocks TODO: shuffle the sbn traversal each call
    for (int sbn = 0; sbn < num_sbn && running; sbn++)
    {
        write_esi(rq, myio, sbn, esi[sbn], buffer, out_mode);
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

void write_configuration_packet(int packet_size, cbuf_handle_t buffer, output_mode_t out_mode)
{
    uint8_t data[packet_size + RQ_HEADER_SIZE];

    memset(data, 0, packet_size);

    if (out_mode == OUTPUT_SHM)
    {
        write_buffer(buffer, configuration_packet, CONFIG_PACKET_SIZE);
        // stuffing bytes... could be used for something useful later on
        if (packet_size > CONFIG_PACKET_SIZE)
            write_buffer(buffer, data, packet_size - CONFIG_PACKET_SIZE);
    }
    else // OUTPUT_TCP
    {
        // For TCP, send full frame with stuffing
        uint8_t full_packet[packet_size];
        memset(full_packet, 0, packet_size);
        memcpy(full_packet, configuration_packet, CONFIG_PACKET_SIZE);
        tcp_interface_send_kiss(&tcp_iface, full_packet, packet_size);
    }
}


void print_usage(const char *prog_name)
{
    printf("Usage: %s [options] file_to_transmit mercury_modulation_mode\n", prog_name);
    printf("\nOptions:\n");
    printf("  -t, --tcp         Use TCP output to hermes-modem (default: shared memory)\n");
    printf("  -i, --ip IP       IP address of hermes-modem (default: %s)\n", DEFAULT_MODEM_IP);
    printf("  -p, --port PORT   TCP port of hermes-modem (default: %d)\n", DEFAULT_MODEM_PORT);
    printf("  -h, --help        Show this help message\n");
    printf("\nmercury_modulation_mode ranges from 0 to 16 (inclusive)\n");
}

int main(int argc, char *argv[]) {

    output_mode_t out_mode = OUTPUT_SHM;
    char *tcp_ip = DEFAULT_MODEM_IP;
    int tcp_port = DEFAULT_MODEM_PORT;

    static struct option long_options[] = {
        {"tcp",  no_argument,       0, 't'},
        {"ip",   required_argument, 0, 'i'},
        {"port", required_argument, 0, 'p'},
        {"help", no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    int option_index = 0;
    while ((opt = getopt_long(argc, argv, "ti:p:h", long_options, &option_index)) != -1)
    {
        switch (opt)
        {
        case 't':
            out_mode = OUTPUT_TCP;
            break;
        case 'i':
            tcp_ip = optarg;
            break;
        case 'p':
            tcp_port = atoi(optarg);
            break;
        case 'h':
            print_usage(argv[0]);
            return 0;
        default:
            print_usage(argv[0]);
            return -1;
        }
    }

    if (argc - optind < 2)
    {
        print_usage(argv[0]);
        return -1;
    }

    char *infile = argv[optind];
    int mod_mode = strtol(argv[optind + 1], NULL, 10);

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
    printf("RaptorQ init: Blocks: %d  Packet_size: %lu\n", num_sbn, packet_size);

    for (int b = 0; b < num_sbn; b++)
    {
        nanorq_generate_symbols(rq, b, myio);
    }

    memset(configuration_packet, 0, CONFIG_PACKET_SIZE);

    nanorq_oti_common_reduced(rq, configuration_packet+1); // 5 bytes
    nanorq_oti_scheme_specific_align1(rq, configuration_packet+6); // 3 bytes

    configuration_packet[0] = (PACKET_RQ_CONFIG << 6) & 0xff;
    configuration_packet[0] |= crc6_0X6F(1, configuration_packet + HERMES_SIZE, CONFIG_PACKET_SIZE - HERMES_SIZE);

    cbuf_handle_t buffer = NULL;

    // Initialize output interface
    if (out_mode == OUTPUT_TCP)
    {
        tcp_interface_init(&tcp_iface, tcp_ip, tcp_port);
        if (!tcp_interface_connect(&tcp_iface))
        {
            fprintf(stderr, "Failed to connect to hermes-modem at %s:%d\n", tcp_ip, tcp_port);
            nanorq_free(rq);
            myio->destroy(myio);
            return -1;
        }
        printf("Output mode: TCP to hermes-modem (%s:%d)\n", tcp_ip, tcp_port);
    }
    else
    {
        buffer = circular_buf_connect_shm(SHM_PAYLOAD_BUFFER_SIZE, SHM_PAYLOAD_NAME);
        if (buffer == NULL)
        {
            fprintf(stderr, "Failed to connect to shared memory\n");
            nanorq_free(rq);
            myio->destroy(myio);
            return -1;
        }
        printf("Output mode: Shared memory\n");
    }

    while(running)
    {
        // 1 configuration packet per each sbn "slice"
        write_configuration_packet(mercury_frame_size[mod_mode], buffer, out_mode);

        if (write_interleaved_block_packets(rq, myio, esi, buffer, out_mode) == false)
            running = false;
    }

    printf("\nshutdown.\n");
    printf("\e[?25h"); // re-enable cursor

    nanorq_free(rq);
    myio->destroy(myio);

    if (out_mode == OUTPUT_TCP)
    {
        tcp_interface_disconnect(&tcp_iface);
    }
    else
    {
        circular_buf_free_shm(buffer);
    }

    return 0;
}
