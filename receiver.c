
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "ring_buffer_posix.h"
#include "mercury_modes.h"
#include "crc6.h"

#include <nanorq.h>

#define SHM_PAYLOAD_BUFFER_SIZE 131072
#define CONFIG_PACKET_SIZE 9
#define MAX_BLOCKS 128
#define SHM_PAYLOAD_NAME "/mercury-comm"

uint8_t configuration_packet[CONFIG_PACKET_SIZE];
bool block_decoded[MAX_BLOCKS];

int main(int argc, char *argv[])
{

    if (argc < 2)
    {
        printf("Usage: %s file_to_receive\n", argv[0]);
        return -1;
    }
    char *outfile = argv[1];
    struct ioctx *myio = ioctx_from_file(outfile, 0);

    if (!myio) {
        fprintf(stdout, "couldnt access file %s\n", outfile);
        return -1;
    }

    uint32_t oti_scheme = 0;
    uint64_t oti_common = 0;

    cbuf_handle_t buffer = circular_buf_connect_shm(SHM_PAYLOAD_BUFFER_SIZE, SHM_PAYLOAD_NAME);

    if (buffer == NULL)
    {
        fprintf(stderr, "Shared memory not created\n");
        return 0;
    }

    read_buffer(buffer, configuration_packet, CONFIG_PACKET_SIZE);

    int packet_type = (configuration_packet[0] >> 6) & 0x3;
    printf("Packet type: %d (0x00 raw, 0x01 uucp, 0x02 rq_config, 0x03 rq_payload)\n", packet_type);
    uint16_t crc6_local = configuration_packet[0] & 0x3f;
    uint16_t crc6_calc = crc6_0X6F(1, configuration_packet+1, 8);

    if (crc6_local != crc6_calc)
    {
        printf("CRC does not match!\n");
        abort();
    }

    /* transfer length */
    oti_common |= (uint64_t) (configuration_packet[1] & 0xff) << 24;
    oti_common |= (uint64_t) (configuration_packet[2] & 0xff) << 32;
    oti_common |= (uint64_t) (configuration_packet[3] & 0xff) << 40;
    /* symbol size */
    oti_common |= configuration_packet[4] & 0xff;
    oti_common |= (configuration_packet[5] & 0xff) << 8;

    /* number of source blocks */
    oti_scheme |= (configuration_packet[6] & 0xff) << 24;
    /* number of sub-blocks */
    oti_scheme |= (configuration_packet[7] & 0xff) << 8;
    oti_scheme |= (configuration_packet[8] & 0xff) << 16;
    /* symbol alignment */
    oti_scheme |=  1;

    printf("size oti_common: %lu %lu\n", sizeof(oti_common), oti_common);
    printf("size oti_scheme: %lu %u\n", sizeof(oti_scheme), oti_scheme);

    nanorq *rq = nanorq_decoder_new(oti_common, oti_scheme);
    if (rq == NULL)
    {
        fprintf(stdout, "Could not initialize decoder.\n");
        return -1;
    }

    int num_sbn = nanorq_blocks(rq);
    uint32_t tag;
    size_t packet_size = nanorq_symbol_size(rq);
    printf("packet size: %lu\n", packet_size);

    uint32_t esi[num_sbn];

    memset(esi, 0, num_sbn * sizeof(uint32_t));
    memset(block_decoded, 0, MAX_BLOCKS * sizeof(bool));

    uint8_t packet[packet_size];

    bool have_more_symbols = false;
    int dups = 0;
    while (1)
    {
        read_buffer(buffer, (uint8_t *) &tag, sizeof(tag));
        read_buffer(buffer, packet, packet_size);

        uint8_t sbn = (tag >> 24) & 0xff;
        // uint32_t esi = (tag & 0x00ffffff);

        int ret = nanorq_decoder_add_symbol(rq, (void *)packet, tag, myio);
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

        fprintf(stdout, "Block %d, esi_sbn %d is %d packets, lost %d, dups %d have %d repair\n", sbn, esi[sbn],
                (unsigned)nanorq_block_symbols(rq, sbn),
                (unsigned)nanorq_num_missing(rq, sbn),
                dups,
                (unsigned)nanorq_num_repair(rq, sbn));

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
            goto success;
        }
        have_more_symbols = false;
    }

success:

    nanorq_free(rq);
    myio->destroy(myio);

    circular_buf_free_shm(buffer);

    return 0;
}
