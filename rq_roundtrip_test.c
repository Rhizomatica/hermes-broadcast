/* nanorq round-trip regression test for hermes-broadcast.
 *
 * Exists to make the nanorq re-vendor checkable.  The vendored copy carries
 * three HERMES-local additions -- nanorq_tag_reduced,
 * nanorq_oti_common_reduced, nanorq_oti_scheme_specific_align1 -- which shrink
 * the per-frame tag to 3 bytes and the config packet to 9, and that is the
 * whole reason RaptorQ fits Mercury's small frames.  They are easy to lose in
 * an upstream merge and their loss would not fail a build: transmitter.c would
 * simply stop linking, or worse, a subtly different byte layout would encode
 * fine and never decode on the far side.
 *
 * So this drives the REAL wire path: encode with the transmitter's parameters,
 * serialise the OTI and tags exactly as transmitter.c does, reconstruct them
 * exactly as receiver.c does, drop symbols to simulate a lossy HF channel, and
 * require the recovered file to be byte-identical.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "raptorq/include/nanorq.h"
#include "mercury_modes.h"

/* transmitter.c caps the ESI at 16 bits because the reduced tag only has two
 * bytes for it.  Mirrored here rather than shared, for the same reason the
 * parse helpers are copied. */
#define RQ_TEST_MAX_ESI ((1 << 16) - 1)

/* Byte-for-byte copies of receiver.c's parse functions.  Copied deliberately
 * rather than shared: if the encoder side changes layout, this test must fail
 * rather than quietly follow it. */
static uint64_t parse_oti_common(const uint8_t *p)
{
    uint64_t c = 0;
    c |= (uint64_t)(p[1] & 0xff) << 24;
    c |= (uint64_t)(p[2] & 0xff) << 32;
    c |= (uint64_t)(p[3] & 0xff) << 40;
    c |= p[4] & 0xff;
    c |= (uint64_t)(p[5] & 0xff) << 8;
    return c;
}

static uint32_t parse_oti_scheme(const uint8_t *p)
{
    uint32_t s = 0;
    s |= (uint32_t)(p[6] & 0xff) << 24;
    s |= (uint32_t)(p[7] & 0xff) << 8;
    s |= (uint32_t)(p[8] & 0xff) << 16;
    s |= 1;                     /* Al is always 1; receiver.c saves the byte */
    return s;
}

static int run_case(size_t filesize, uint32_t frame_size, int drop_every, unsigned seed)
{
    int rc = -1;
    uint8_t *src = malloc(filesize), *dst = NULL;
    struct ioctx *tio = NULL, *rio = NULL;
    nanorq *enc = NULL, *dec = NULL;
    const char *tf = "/tmp/.rq_rt_tx.bin", *rf = "/tmp/.rq_rt_rx.bin";

    srand(seed);
    for (size_t i = 0; i < filesize; i++) src[i] = (uint8_t)(rand() & 0xff);
    FILE *f = fopen(tf, "wb"); fwrite(src, 1, filesize, f); fclose(f);

    size_t packet_size = frame_size - RQ_HEADER_SIZE;
    uint8_t *pkt = malloc(frame_size);
    enc = nanorq_encoder_new(filesize, packet_size, 1);
    if (!enc) { fprintf(stderr, "  encoder_new failed\n"); goto out; }
    nanorq_set_max_esi(enc, RQ_TEST_MAX_ESI);

    tio = ioctx_from_file(tf, 1);
    if (!tio) { fprintf(stderr, "  ioctx tx failed\n"); goto out; }

    int nsbn = nanorq_blocks(enc);
    packet_size = nanorq_symbol_size(enc);
    for (int b = 0; b < nsbn; b++) nanorq_generate_symbols(enc, b, tio);

    /* --- the config packet, exactly as transmitter.c builds it --- */
    uint8_t cfg[CONFIG_PACKET_SIZE];
    memset(cfg, 0, sizeof(cfg));
    nanorq_oti_common_reduced(enc, cfg + 1);
    nanorq_oti_scheme_specific_align1(enc, cfg + 6);

    /* --- decoder built from the reduced bytes, as receiver.c does --- */
    dec = nanorq_decoder_new(parse_oti_common(cfg), parse_oti_scheme(cfg));
    if (!dec) { fprintf(stderr, "  decoder_new failed from reduced OTI\n"); goto out; }
    if (nanorq_transfer_length(dec) != filesize) {
        fprintf(stderr, "  reduced OTI lost the length: got %zu want %zu\n",
                nanorq_transfer_length(dec), filesize);
        goto out;
    }

    rio = ioctx_from_file(rf, 0);
    if (!rio) { fprintf(stderr, "  ioctx rx failed\n"); goto out; }

    /* --- carousel with loss --- */
    int sent = 0;
    bool done = false;
    for (uint32_t esi = 0; esi < RQ_TEST_MAX_ESI && !done; esi++) {
        for (int sbn = 0; sbn < nsbn && !done; sbn++) {
            memset(pkt, 0, frame_size);
            if (nanorq_encode(enc, pkt + RQ_HEADER_SIZE, esi, sbn, tio) != packet_size) {
                fprintf(stderr, "  encode failed sbn=%d esi=%u\n", sbn, esi); goto out;
            }
            nanorq_tag_reduced((uint8_t)sbn, esi, pkt + 1);   /* 3-byte tag */
            sent++;
            if (drop_every && (sent % drop_every) == 0) continue;  /* lost */

            /* receiver side: rebuild the 32-bit tag from the 3 reduced bytes */
            uint32_t tag = ((uint32_t)pkt[1] << 24) | pkt[2] | ((uint32_t)pkt[3] << 8);
            nanorq_decoder_add_symbol(dec, pkt + RQ_HEADER_SIZE, tag, rio);

            done = true;
            for (int b = 0; b < nsbn; b++)
                if (!nanorq_repair_block(dec, rio, b)) { done = false; break; }
        }
    }
    if (!done) { fprintf(stderr, "  never decoded after %d packets\n", sent); goto out; }
    rio->destroy(rio); rio = NULL;

    dst = malloc(filesize);
    f = fopen(rf, "rb");
    size_t got = f ? fread(dst, 1, filesize, f) : 0;
    if (f) fclose(f);
    if (got != filesize || memcmp(src, dst, filesize) != 0) {
        fprintf(stderr, "  MISMATCH (read %zu of %zu)\n", got, filesize);
        goto out;
    }
    printf("  ok  file=%6zu frame=%4u drop=1/%-3d  decoded after %d packets\n",
           filesize, frame_size, drop_every ? drop_every : 0, sent);
    rc = 0;
out:
    if (enc) nanorq_free(enc);
    if (dec) nanorq_free(dec);
    if (tio) tio->destroy(tio);
    if (rio) rio->destroy(rio);
    free(src); free(dst); free(pkt);
    remove(tf); remove(rf);
    return rc;
}

int main(void)
{
    int bad = 0;
    printf("nanorq round-trip through the HERMES reduced OTI/tag path\n");
    /* frame sizes are real Mercury modes: DATAC3=126, DATAC1=510, DATAC17=1180 */
    bad |= run_case(20000,  126, 0,  1);
    bad |= run_case(20000,  126, 7,  2);
    bad |= run_case(64000,  510, 5,  3);
    bad |= run_case(64000, 1180, 4,  4);
    bad |= run_case(3000,   510, 3,  5);
    printf(bad ? "FAILED\n" : "all round-trips passed\n");
    return bad ? 1 : 0;
}
