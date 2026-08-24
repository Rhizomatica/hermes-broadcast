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

#include "mercury_modes.h"
#include "tcp_interface.h"

#include <nanorq.h>


#define MAX_ESI 65535

bool running;

uint8_t configuration_packet[CONFIG_PACKET_SIZE];

// Global TCP interface
tcp_interface_t tcp_iface;
static uint64_t tx_config_packets = 0;
static uint64_t tx_payload_packets = 0;

void exit_system(int sig)
{
    printf("\nExiting... ");
    running = false;
}

void write_esi(nanorq *rq, struct ioctx *myio, uint8_t sbn, uint32_t esi)
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

        hermes_write_frame_header(data, PACKET_RQ_PAYLOAD, 0);

        tcp_interface_send_kiss(&tcp_iface, data, packet_size + RQ_HEADER_SIZE);
        tx_payload_packets++;
        if ((tx_payload_packets % 100) == 0)
        {
            fprintf(stderr, "\n[DBG TX] payload_sent=%llu config_sent=%llu\n",
                    (unsigned long long)tx_payload_packets,
                    (unsigned long long)tx_config_packets);
        }
        fprintf(stdout, "\rBlock: %2d  Tx: %3d",  sbn, esi);
        fflush(stdout);
        // for (int i = 0; i < packet_size + RQ_HEADER_SIZE; i++)
        //    printf("%02x ", data[i]);
        // printf("\n");
    }
}

bool write_interleaved_block_packets(nanorq *rq, struct ioctx *myio, uint32_t *esi)
{
    int num_sbn = nanorq_blocks(rq);

    // for all blocks TODO: shuffle the sbn traversal each call
    for (int sbn = 0; sbn < num_sbn && running; sbn++)
    {
        write_esi(rq, myio, sbn, esi[sbn]);
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

void write_configuration_packet(uint32_t frame_size)
{
    // Emit a FULL modem frame: the config packet itself plus zero stuffing out
    // to frame_size.  The payload path sends packet_size + RQ_HEADER_SIZE,
    // which is the same total, and our receiver accepts a TCP frame only when
    // its length is exactly frame_size.
    uint8_t full_packet[frame_size];

    memset(full_packet, 0, frame_size);
    memcpy(full_packet, configuration_packet, CONFIG_PACKET_SIZE);

    tcp_interface_send_kiss(&tcp_iface, full_packet, (int)frame_size);

    tx_config_packets++;
    if (tx_config_packets <= 10 || (tx_config_packets % 50) == 0)
    {
        fprintf(stderr, "\n[DBG TX] sent config packet #%llu (size=%u)\n",
                (unsigned long long)tx_config_packets, frame_size);
    }
}


void print_usage(const char *prog_name)
{
    printf("Usage: %s [options] file_to_transmit modulation_mode\n", prog_name);
    printf("\nOptions:\n");
    printf("  -t, --tcp         Accepted for compatibility (TCP is the only output)\n");
    printf("  -i, --ip IP       IP address of mercury (default: %s)\n", DEFAULT_MODEM_IP);
    printf("  -p, --port PORT   TCP port of mercury (default: %d)\n", DEFAULT_MODEM_PORT);
    printf("  -h, --help        Show this help message\n");
    printf("\nModulation modes (mercury payload bytes per modem frame):\n");
    printf("    Mode  0: DATAC1   -  510 bytes\n");
    printf("    Mode  1: DATAC3   -  126 bytes\n");
    printf("    Mode  2: DATAC0   -   14 bytes\n");
    printf("    Mode  3: DATAC4   -   54 bytes\n");
    printf("    Mode  4: DATAC13  -   14 bytes\n");
    printf("    Mode  5: DATAC14  -    3 bytes\n");
    printf("    Mode  6: FSK_LDPC -   30 bytes\n");
    printf("    Mode  7: DATAC15  -   30 bytes\n");
    printf("    Mode  8: DATAC16  -   14 bytes\n");
    printf("    Mode  9: DATAC17  - 1180 bytes\n");
    printf("    Mode 10: QAM16C2  - 1213 bytes\n");
}

int main(int argc, char *argv[]) {

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
            // TCP is the only output; accepted so existing scripts keep working
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

    int max_mode = HERMES_MODE_MAX;
    uint32_t *frame_sizes = hermes_frame_size;

    if (mod_mode < 0 || mod_mode > max_mode)
    {
        printf("Invalid mode %d. Valid modes range from 0 to %d.\n", mod_mode, max_mode);
        return -1;
    }

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

    uint32_t frame_size = frame_sizes[mod_mode];
    size_t packet_size = frame_size - (uint32_t) RQ_HEADER_SIZE; // T

    printf("Mode: %d, Frame size: %u bytes, Packet size: %zu bytes\n", mod_mode, frame_size, packet_size);

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

    hermes_write_frame_header(configuration_packet, PACKET_RQ_CONFIG, 0);

    // Initialize output interface
    tcp_interface_init(&tcp_iface, tcp_ip, tcp_port);
    if (!tcp_interface_connect(&tcp_iface))
    {
        fprintf(stderr, "Failed to connect to mercury at %s:%d\n", tcp_ip, tcp_port);
        nanorq_free(rq);
        myio->destroy(myio);
        return -1;
    }
    printf("Output mode: TCP to mercury (%s:%d)\n", tcp_ip, tcp_port);

    while(running)
    {
        // 1 configuration packet per each sbn "slice"
        write_configuration_packet(frame_size);

        if (write_interleaved_block_packets(rq, myio, esi) == false)
            running = false;
    }

    printf("\nshutdown.\n");
    printf("\e[?25h"); // re-enable cursor

    nanorq_free(rq);
    myio->destroy(myio);

    tcp_interface_disconnect(&tcp_iface);

    return 0;
}
