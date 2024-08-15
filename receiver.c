
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "ring_buffer_posix.h"
#include "mercury_modes.h"
#include "crc6.h"

// #define ENABLE_LOOP // for debug purposes...


#include <nanorq.h>

#define MAX_ESI 65535

#define MAX_BLOCKS 128

bool block_decoded[MAX_BLOCKS];

int8_t parse_frame_header(uint8_t *data_frame, uint32_t frame_size)
{
    uint8_t packet_type = (data_frame[0] >> 6) & 0x3;

    uint16_t crc6_local = data_frame[0] & 0x3f;
    uint16_t crc6_calc = 0;
    // if packet is rq_config
    switch(packet_type)
    {
    case 0x02: // RaptorQ Configuration Packet
        crc6_calc = crc6_0X6F(1, data_frame + HERMES_SIZE, CONFIG_PACKET_SIZE - HERMES_SIZE);
        break;
    case 0x03: // RaptorQ Payload Packet
        crc6_calc = crc6_0X6F(1, data_frame + HERMES_SIZE, frame_size - HERMES_SIZE);
        break;
    default:
        crc6_calc = crc6_0X6F(1, data_frame + HERMES_SIZE, frame_size - HERMES_SIZE);
        printf("Packet type: %hhu not handled by hermes-broadcast\n", packet_type);
    }

    if (crc6_local != crc6_calc)
    {
        printf("CRC does not match!\n");
        return -1;
    }
    return packet_type;
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

int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        printf("Usage: %s file_to_receive mercury_modulation_mode\n", argv[0]);
        return -1;
    }

    char *outfile = argv[1];
    struct ioctx *myio = ioctx_from_file(outfile, 0);

    if (!myio) {
        fprintf(stdout, "couldnt access file %s\n", outfile);
        return -1;
    }

    int mod_mode = strtol(argv[2], NULL, 10);
    uint32_t frame_size = 0;

    if ((mod_mode <= 16) && (mod_mode >= 0))
        frame_size = mercury_frame_size[mod_mode];
    else
    {
        printf("Valid modes range from 0 to 16 (inclusive).\n");
        exit(-1);
    }

    bool configuration_received = false;

    uint8_t data_frame[frame_size];
    uint32_t oti_scheme = 0;
    uint64_t oti_common = 0;
    int num_sbn = 0;
    uint32_t tag = 0;
    uint32_t esi[MAX_BLOCKS];

    memset(esi, 0, MAX_BLOCKS * sizeof(uint32_t));
    memset(block_decoded, 0, MAX_BLOCKS * sizeof(bool));

    bool have_more_symbols = false;
    int dups = 0;

    nanorq *rq = NULL;
    cbuf_handle_t buffer = circular_buf_connect_shm(SHM_PAYLOAD_BUFFER_SIZE, SHM_PAYLOAD_NAME);

    if (buffer == NULL)
    {
        fprintf(stderr, "Shared memory not created\n");
        return 0;
    }

#ifdef ENABLE_LOOP
try_again:
#endif

    while (1)
    {
        read_buffer(buffer, data_frame, frame_size);

        int8_t packet_type = parse_frame_header(data_frame, frame_size);
        if (packet_type < 0)
            continue; // bad crc

        printf("\rPacket type: 0x%02x (%s) ", packet_type, (packet_type == 0x03)?"rq_payload":(packet_type == 0x02)?"rq_config":"unknown");

        if ((configuration_received == false) &&
            packet_type == PACKET_RQ_CONFIG)
        {
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

            printf("RaptorQ decoder initialized!\n");

            continue;
        }

        if ((configuration_received == true) &&
            packet_type == PACKET_RQ_CONFIG)
        {
            uint64_t oti_common_local = parse_tag_oti_common(data_frame);
            uint32_t oti_scheme_local = parse_tag_oti_scheme(data_frame);

            printf("\n");

            // nanorq_num_repair();
            if((oti_common_local != oti_common) ||
               (oti_scheme_local != oti_scheme))
            {
                printf("need to reset the stuff. TODO\n");
                return (-1);
            }
            continue;
        }
        if ((configuration_received == true) &&
            packet_type == PACKET_RQ_PAYLOAD)
        {
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
                fprintf(stdout, "adding symbol %d failed. Contining...\n", tag);
                continue;
            }

            if (ret == NANORQ_SYM_ADDED)
            {
                esi[sbn]++;
                have_more_symbols = true;
            }
            else if (ret == NANORQ_SYM_DUP)
            {
                dups++;
                have_more_symbols = false;
            }

            fprintf(stdout, "Block: %d  Recv: %d Needed: %lu\n", sbn, esi[sbn], nanorq_block_symbols(rq, sbn));

            // if (esi[sbn] >= nanorq_block_symbols(rq, sbn) && have_more_symbols)
            if (esi[sbn] >= nanorq_block_symbols(rq, sbn) && have_more_symbols)
            {
                if (!nanorq_repair_block(rq, myio, sbn))
                {
                    fprintf(stdout, "decode of sbn %d failed.\n", sbn);
                }
                else
                {
                    fprintf(stdout, "DECODE OF BLOCK %d SUCCESSFUL!.\n", sbn);
                    block_decoded[sbn] = true;
                }
            }
            // write_configuration_packets(buffer);
            bool file_received = true;
            for (int i = 0; i < num_sbn; i++)
            {
                if (block_decoded[i] == false)
                    file_received = false;
            }

            if (file_received == true)
            {
                printf("FILE SUCCESSFULLY RECEIVED!\n");
                goto success;
            }
            have_more_symbols = false;
        }
    }
success:

    nanorq_free(rq);


//enable loop
#ifdef ENABLE_LOOP
    configuration_received = false;
    goto try_again;
#endif
    myio->destroy(myio);

    circular_buf_free_shm(buffer);

    return 0;
}
