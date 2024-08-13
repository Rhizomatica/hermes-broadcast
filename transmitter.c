/* RaptorQ fountain code transmitter
 *
 * Copyright (C) 2020-2024 by Rafael Diniz <rafael@rhizomatica.org>
 * All Rights Reserved
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "ring_buffer_posix.h"

#include <nanorq.h>

#define SHM_PAYLOAD_BUFFER_SIZE 131072
#define CONFIG_PACKET_SIZE 12
#define SHM_PAYLOAD_NAME "/mercury-comm"

uint8_t configuration_packet[CONFIG_PACKET_SIZE];

void write_esi(nanorq *rq, struct ioctx *myio, uint8_t sbn,
              uint32_t esi, cbuf_handle_t buffer)
{
    uint32_t tag = nanorq_tag(sbn, esi);
    size_t packet_size = nanorq_symbol_size(rq);
    uint8_t data[packet_size];
    memset(data, 0, packet_size);
    uint64_t written = nanorq_encode(rq, (void *)data, esi, sbn, myio);

    if (written != packet_size)
    {
        fprintf(stdout, "failed to encode packet data for sbn %d esi %d.", sbn, esi);
        abort();
    }
    else
    {
        fprintf(stdout, "Block written: sbn: %d esi %d tag data size: %lu data size: %lu\n",  sbn, esi, sizeof(tag), packet_size);
        write_buffer(buffer, (uint8_t *)&tag, sizeof(tag));
        write_buffer(buffer, data, packet_size);
    }
}

void write_interleaved_block_packets(nanorq *rq, struct ioctx *myio, uint32_t *esi, cbuf_handle_t buffer)
{
    int num_sbn = nanorq_blocks(rq);

    // for all blocks TODO: shuffle the sbn traversal each call
    for (int sbn = 0; sbn < num_sbn; sbn++)
    {
        int num_esi = nanorq_block_symbols(rq, sbn);
        write_esi(rq, myio, sbn, esi[sbn], buffer);
        esi[sbn]++;
        if (esi[sbn] > ((1 << 24) - 1))
            esi[sbn] = 0;
    }
}

void write_configuration_packets(cbuf_handle_t buffer)
{
    write_buffer(buffer, configuration_packet, CONFIG_PACKET_SIZE);
}


int main(int argc, char *argv[]) {

    if (argc < 3)
    {
        printf("Usage: %s file_to_transmit packet_size\n", argv[0]);
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

  // determine chunks, symbol size, memory usage from size
  size_t packet_size = strtol(argv[2], NULL, 10); // T
  uint8_t align = 1;

  srand((unsigned int)time(0));

  nanorq *rq = nanorq_encoder_new(filesize, packet_size, align);

  if (rq == NULL)
  {
      fprintf(stdout, "Could not initialize encoder.\n");
      return -1;
  }

  int num_sbn = nanorq_blocks(rq);
  packet_size = nanorq_symbol_size(rq);
  uint32_t esi[num_sbn];

  memset(esi, 0, num_sbn * sizeof(uint32_t));

  printf("sbn (blocks) = %d\npacket_size: %lu", num_sbn, packet_size);

  for (int b = 0; b < num_sbn; b++)
  {
      nanorq_generate_symbols(rq, b, myio);
  }

  uint64_t oti_common = nanorq_oti_common(rq);
  uint32_t oti_scheme = nanorq_oti_scheme_specific(rq);

  memcpy(configuration_packet, &oti_common, sizeof(oti_common));
  memcpy(configuration_packet + sizeof(oti_common), &oti_scheme, sizeof(oti_scheme));

  printf("size oti_common: %lu %lu\n", sizeof(oti_common), oti_common);
  printf("size oti_scheme: %lu %u\n", sizeof(oti_scheme), oti_scheme);


  cbuf_handle_t buffer;

  buffer = circular_buf_init_shm(SHM_PAYLOAD_BUFFER_SIZE, (char *) SHM_PAYLOAD_NAME);

  write_configuration_packets(buffer);

  while(1)
  {
      // write_configuration_packets(buffer);
      for (int i = 0; i < num_sbn; i++)
      {
          write_interleaved_block_packets(rq, myio, esi, buffer);
      }
  }


  nanorq_free(rq);
  myio->destroy(myio);

  circular_buf_destroy_shm(buffer, SHM_PAYLOAD_BUFFER_SIZE, (char *) SHM_PAYLOAD_NAME);
  circular_buf_free_shm(buffer);

  return 0;
}
