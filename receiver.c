/* RaptorQ fountain code receiver
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
#include <signal.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>

#include "mercury_modes.h"
#include "tcp_interface.h"
#include "kiss.h"

// #define ENABLE_LOOP // for debug purposes...


#include <nanorq.h>

#define MAX_ESI 65535

#define MAX_BLOCKS 128

bool block_decoded[MAX_BLOCKS];

bool running;

// Global TCP interface
tcp_interface_t tcp_iface;

void exit_system(int sig)
{
    printf("Exiting... ");
    running = false;
}

static const char *packet_type_name(uint8_t packet_type)
{
    switch (packet_type)
    {
    case PACKET_RQ_CONFIG:
        return "rq_config";
    case PACKET_RQ_PAYLOAD:
        return "rq_payload";
    default:
        return "unknown";
    }
}

int8_t parse_frame_header(uint8_t *data_frame, uint32_t frame_size)
{
    if (!data_frame || frame_size < HERMES_SIZE)
        return -1;

    return (int8_t)hermes_frame_packet_type(data_frame[0]);
}

uint64_t parse_tag_oti_common(uint8_t *packet)
{
    uint64_t oti_common = 0;

    /* transfer length */
    oti_common |= (uint64_t) (packet[1] & 0xff) << 24;
    oti_common |= (uint64_t) (packet[2] & 0xff) << 32;
    oti_common |= (uint64_t) (packet[3] & 0xff) << 40;

    /* symbol size */
    oti_common |= packet[4] & 0xff;
    oti_common |= (packet[5] & 0xff) << 8;

    return oti_common;
}

uint32_t parse_tag_oti_scheme(uint8_t *packet)
{
    uint32_t oti_scheme = 0;

    /* number of source blocks */
    oti_scheme |= (packet[6] & 0xff) << 24;

    /* number of sub-blocks */
    oti_scheme |= (packet[7] & 0xff) << 8;
    oti_scheme |= (packet[8] & 0xff) << 16;

    /* symbol alignment */
    oti_scheme |=  1; // we are saving 1 byte here...
    return oti_scheme;
}

void print_usage(const char *prog_name)
{
    printf("Usage: %s [options] file_to_receive modulation_mode\n", prog_name);
    printf("\nOptions:\n");
    printf("  -t, --tcp         Accepted for compatibility (TCP is the only input)\n");
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

// Read a KISS-framed frame from mercury
// Returns 1 on success, 0 if no data available, -1 on error
int read_frame_from_input(uint8_t *data_frame, uint32_t frame_size, uint32_t *rx_frame_len)
{
    if (rx_frame_len)
        *rx_frame_len = 0;

    int frame_len = tcp_interface_recv_kiss(&tcp_iface, data_frame);

    if (frame_len < 0)
        return -1;  // Error or disconnected

    if (frame_len == 0)
        return 0;   // No complete frame yet

    if ((uint32_t)frame_len != frame_size)
    {
        fprintf(stderr, "Discarding unexpected TCP frame length %d (expected %u)\n",
                frame_len, frame_size);
        return 0;
    }

    if (rx_frame_len)
        *rx_frame_len = (uint32_t)frame_len;

    return 1;
}

int main(int argc, char *argv[])
{
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
            // TCP is the only input; accepted so existing scripts keep working
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

    char *outfile = argv[optind];
    int mod_mode = strtol(argv[optind + 1], NULL, 10);

    int max_mode = HERMES_MODE_MAX;
    uint32_t *frame_sizes = hermes_frame_size;

    if (mod_mode < 0 || mod_mode > max_mode)
    {
        printf("Invalid mode %d. Valid modes range from 0 to %d.\n", mod_mode, max_mode);
        return -1;
    }

    struct ioctx *myio = ioctx_from_file(outfile, 0);

    if (!myio) {
        fprintf(stdout, "couldnt access file %s\n", outfile);
        return -1;
    }

    uint32_t frame_size = frame_sizes[mod_mode];

    // See the transmitter: a frame smaller than the RaptorQ tag plus the
    // configuration packet cannot carry the broadcast protocol.
    if (frame_size <= RQ_HEADER_SIZE || frame_size < CONFIG_PACKET_SIZE)
    {
        printf("Mode %d carries only %u bytes per frame; broadcast needs at least %d.\n",
               mod_mode, frame_size, CONFIG_PACKET_SIZE);
        return -1;
    }

    printf("Mode: %d, Frame size: %u bytes\n", mod_mode, frame_size);

    running = true;
    signal(SIGQUIT, exit_system);
    signal(SIGINT, exit_system);
    signal(SIGTERM, exit_system);

    bool configuration_received = false;

    uint8_t data_frame[MAX_PAYLOAD];
    uint32_t rx_frame_len = 0;
    uint32_t oti_scheme = 0;
    uint64_t oti_common = 0;
    int num_sbn = 0;
    uint32_t tag = 0;
    uint32_t esi[MAX_BLOCKS];

    memset(esi, 0, MAX_BLOCKS * sizeof(uint32_t));
    memset(block_decoded, 0, MAX_BLOCKS * sizeof(bool));

    bool have_more_symbols = false;

    nanorq *rq = NULL;
    // Initialize input interface
    tcp_interface_init(&tcp_iface, tcp_ip, tcp_port);
    if (!tcp_interface_connect(&tcp_iface))
    {
        fprintf(stderr, "Failed to connect to mercury at %s:%d\n", tcp_ip, tcp_port);
        myio->destroy(myio);
        return -1;
    }
    printf("Input mode: TCP from mercury (%s:%d)\n", tcp_ip, tcp_port);

#ifdef ENABLE_LOOP
try_again:
#endif
    printf("\e[?25l"); // hide cursor
    uint32_t spinner_anim = 0; char spinner[] = ".oOo";
    uint64_t total_frames = 0;
    uint64_t header_errors = 0;
    uint64_t config_packets = 0;
    uint64_t payload_packets = 0;
    uint64_t symbols_added = 0;
    uint64_t symbols_dup = 0;
    uint64_t symbols_err = 0;
    uint64_t size_mismatch_packets = 0;
    uint64_t decoded_blocks = 0;
    uint64_t payload_before_config = 0;
    while (running)
    {
        int read_result = read_frame_from_input(data_frame, frame_size, &rx_frame_len);
        if (read_result == 0)
        {
            usleep(100000); // 0.1s - shorter for TCP mode
            continue;
        }
        else if (read_result < 0)
        {
            fprintf(stderr, "Error reading from input\n");
            break;
        }

        total_frames++;
        if (rx_frame_len != frame_size && rx_frame_len >= CONFIG_PACKET_SIZE)
            size_mismatch_packets++;

        int8_t packet_type = parse_frame_header(data_frame, rx_frame_len);
        if (packet_type < 0)
        {
            header_errors++;
            continue;
        }

        printf("\x1b[2K\rPkt: 0x%02x (%s) %c ",
               (unsigned int)(uint8_t)packet_type,
               packet_type_name((uint8_t)packet_type),
               spinner[spinner_anim % 4]);
        spinner_anim++; fflush(stdout);

        if (configuration_received == false && packet_type == PACKET_RQ_CONFIG)
        {
            config_packets++;
            oti_common = parse_tag_oti_common(data_frame);
            oti_scheme = parse_tag_oti_scheme(data_frame);

            // printf("size oti_common: %lu %lu\n", sizeof(oti_common), oti_common);
            // printf("size oti_scheme: %lu %u\n", sizeof(oti_scheme), oti_scheme);

            rq = nanorq_decoder_new(oti_common, oti_scheme);
            if (rq == NULL)
            {
                fprintf(stdout, "Could not initialize decoder.\n");
                continue;
            }

            nanorq_set_max_esi(rq, MAX_ESI);

            num_sbn = nanorq_blocks(rq);

            configuration_received = true;

            printf(" RaptorQ initialized!"); fflush(stdout);

            continue;
        }

        if (configuration_received == true && packet_type == PACKET_RQ_CONFIG)
        {
            config_packets++;
            uint64_t oti_common_local = parse_tag_oti_common(data_frame);
            uint32_t oti_scheme_local = parse_tag_oti_scheme(data_frame);

            // nanorq_num_repair();
            if((oti_common_local != oti_common) ||
               (oti_scheme_local != oti_scheme))
            {
                printf("Need to reset the system to new file. TODO!\n");
                running = false;
                break;
            }
            continue;
        }
        if ((configuration_received == true) &&
            packet_type == PACKET_RQ_PAYLOAD)
        {
            payload_packets++;
            // for (int i = 0; i < frame_size; i++)
            //    printf("%02x ", data_frame[i]);
            // printf("\n");

            uint8_t sbn = data_frame[1];
            uint32_t esi_local = (uint32_t) data_frame[2] | ((uint32_t) data_frame[3] << 8);
            tag = nanorq_tag(sbn, esi_local);
            // printf("Packet received: %u SBN: %hhu ESI %u\n", tag, sbn, esi_local);

            int ret = nanorq_decoder_add_symbol(rq, (void *)data_frame + RQ_HEADER_SIZE, tag, myio);
            if (NANORQ_SYM_ERR == ret)
            {
                symbols_err++;
                fprintf(stdout, "adding symbol %d failed. Contining...\n", tag);
                continue;
            }

            if (ret == NANORQ_SYM_ADDED)
            {
                symbols_added++;
                esi[sbn]++;
                have_more_symbols = true;
            }
            else if (ret == NANORQ_SYM_DUP)
            {
                symbols_dup++;
                have_more_symbols = false;
            }

            fprintf(stdout, "Blk: %3d  Recv: %3d of %3lu", sbn, esi[sbn], nanorq_block_symbols(rq, sbn));
            fflush(stdout);

            // if (esi[sbn] >= nanorq_block_symbols(rq, sbn) && have_more_symbols)
            if (esi[sbn] >= nanorq_block_symbols(rq, sbn) && have_more_symbols)
            {
                if (!nanorq_repair_block(rq, myio, sbn))
                {
                    fprintf(stdout, "Decode of sbn %d failed. Continuing...\n", sbn);
                }
                else
                {
                    fprintf(stdout, "\x1b[2K\rDECODE OF BLOCK %d SUCCESSFUL!", sbn);
                    if (!block_decoded[sbn])
                    {
                        block_decoded[sbn] = true;
                        decoded_blocks++;
                    }
                }
            }
            bool file_received = true;
            for (int i = 0; i < num_sbn; i++)
            {
                if (block_decoded[i] == false)
                    file_received = false;
            }

            if (file_received == true)
            {
                printf("\x1b[2K\rFILE SUCCESSFULLY RECEIVED!\n");
                goto success;
            }
            have_more_symbols = false;
        }

        if (!configuration_received && packet_type == PACKET_RQ_PAYLOAD)
        {
            payload_before_config++;
            if (payload_before_config <= 10 || (payload_before_config % 20) == 0)
            {
                fprintf(stderr,
                        "\n[DBG RX] payload before config: %llu (total=%llu)\n",
                        (unsigned long long)payload_before_config,
                        (unsigned long long)total_frames);
            }
        }

        if ((total_frames % 50) == 0)
        {
            fprintf(stderr,
                    "\n[DBG RX] total=%llu cfg=%llu payload=%llu hdr_err=%llu sym_added=%llu sym_dup=%llu sym_err=%llu decoded=%llu/%d len=%u mismatch=%llu pre_cfg_payload=%llu\n",
                    (unsigned long long)total_frames,
                    (unsigned long long)config_packets,
                    (unsigned long long)payload_packets,
                    (unsigned long long)header_errors,
                    (unsigned long long)symbols_added,
                    (unsigned long long)symbols_dup,
                    (unsigned long long)symbols_err,
                    (unsigned long long)decoded_blocks,
                    num_sbn,
                    rx_frame_len,
                    (unsigned long long)size_mismatch_packets,
                    (unsigned long long)payload_before_config);
            fflush(stderr);
        }
    }
success:

    printf("shutdown.\n");
    printf("\e[?25h"); // re-enable cursor
    if (rq)
        nanorq_free(rq);

//enable loop
#ifdef ENABLE_LOOP
    configuration_received = false;
    goto try_again;
#endif
    myio->destroy(myio);

    tcp_interface_disconnect(&tcp_iface);

    return 0;
}
