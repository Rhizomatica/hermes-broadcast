/* Broadcast TX/RX daemon with joint RaptorQ configuration and payload frames
 *
 * Copyright (C) 2026 Rhizomatica
 * Author: Rafael Diniz <rafael@riseup.net>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <getopt.h>

#include "crc6.h"
#include "kiss.h"
#include "mercury_modes.h"
#include "tcp_interface.h"

#include <nanorq.h>

#define CONFIG_BODY_SIZE 8
#define TAG_BODY_SIZE 3
#define MAX_ESI 65535

typedef struct {
    int mode;
    uint32_t frame_size;
    uint32_t symbol_size;
    bool verbose;
    char tx_dir[PATH_MAX];
    char rx_dir[PATH_MAX];
    tcp_interface_t tcp_iface;
} daemon_ctx_t;

typedef struct {
    bool active;
    char file_path[PATH_MAX];
    time_t mtime;
    int64_t frames_limit;   // -1 means continuous
    int64_t frames_sent;
    int next_sbn;
    uint8_t config_body[CONFIG_BODY_SIZE];
    struct ioctx *myio;
    nanorq *rq;
    uint32_t *esi;
    int num_sbn;
} tx_session_t;

typedef struct {
    bool active;
    bool completed_last;
    uint64_t last_completed_oti_common;
    uint32_t last_completed_oti_scheme;
    uint64_t oti_common;
    uint32_t oti_scheme;
    int num_sbn;
    char out_path[PATH_MAX];
    struct ioctx *myio;
    nanorq *rq;
    bool *block_decoded;
    uint32_t *block_symbols_seen;
} rx_session_t;

static volatile sig_atomic_t running = 1;

static void handle_signal(int sig)
{
    (void)sig;
    running = 0;
}

static void tx_session_reset(tx_session_t *tx)
{
    if (tx->rq) nanorq_free(tx->rq);
    if (tx->myio) tx->myio->destroy(tx->myio);
    free(tx->esi);
    memset(tx, 0, sizeof(*tx));
}

static void rx_session_reset(rx_session_t *rx)
{
    if (rx->rq) nanorq_free(rx->rq);
    if (rx->myio) rx->myio->destroy(rx->myio);
    free(rx->block_decoded);
    free(rx->block_symbols_seen);
    bool completed_last = rx->completed_last;
    uint64_t last_common = rx->last_completed_oti_common;
    uint32_t last_scheme = rx->last_completed_oti_scheme;
    memset(rx, 0, sizeof(*rx));
    rx->completed_last = completed_last;
    rx->last_completed_oti_common = last_common;
    rx->last_completed_oti_scheme = last_scheme;
}

static int64_t parse_frames_limit_from_filename(const char *filepath)
{
    const char *base = strrchr(filepath, '/');
    base = base ? base + 1 : filepath;

    const char *suffix = strstr(base, "_frames");
    if (!suffix) return -1;

    const char *start = suffix;
    while (start > base && isdigit((unsigned char)*(start - 1))) start--;
    if (start <= base || *(start - 1) != '-') return -1;

    char tmp[32];
    size_t n = (size_t)(suffix - start);
    if (n == 0 || n >= sizeof(tmp)) return -1;
    memcpy(tmp, start, n);
    tmp[n] = '\0';

    long long val = atoll(tmp);
    return (val > 0) ? (int64_t)val : -1;
}

static bool find_first_regular_file(const char *dirpath, char *out_path, size_t out_path_len, time_t *mtime_out)
{
    DIR *d = opendir(dirpath);
    if (!d) return false;

    bool found = false;
    char best_name[NAME_MAX + 1] = {0};
    struct dirent *de;
    while ((de = readdir(d)) != NULL)
    {
        if (de->d_name[0] == '.') continue;
        char candidate[PATH_MAX];
        snprintf(candidate, sizeof(candidate), "%s/%s", dirpath, de->d_name);
        struct stat st;
        if (stat(candidate, &st) != 0) continue;
        if (!S_ISREG(st.st_mode)) continue;
        if (!found || strcmp(de->d_name, best_name) < 0)
        {
            found = true;
            snprintf(best_name, sizeof(best_name), "%s", de->d_name);
        }
    }
    closedir(d);

    if (!found) return false;

    snprintf(out_path, out_path_len, "%s/%s", dirpath, best_name);
    struct stat st;
    if (stat(out_path, &st) != 0) return false;
    if (mtime_out) *mtime_out = st.st_mtime;
    return true;
}

static bool build_output_path(const char *rx_dir, char *out_path, size_t out_path_len)
{
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);

    for (int i = 0; i < 1000; i++)
    {
        if (i == 0)
        {
            snprintf(out_path, out_path_len, "%s/broadcast_%04d%02d%02d_%02d%02d%02d.bin",
                     rx_dir,
                     tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday,
                     tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);
        }
        else
        {
            snprintf(out_path, out_path_len, "%s/broadcast_%04d%02d%02d_%02d%02d%02d_%03d.bin",
                     rx_dir,
                     tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday,
                     tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec, i);
        }

        if (access(out_path, F_OK) != 0) return true;
    }
    return false;
}

static bool tx_session_open(daemon_ctx_t *ctx, tx_session_t *tx, const char *file_path, time_t mtime)
{
    tx_session_reset(tx);

    tx->myio = ioctx_from_file(file_path, 1);
    if (!tx->myio)
    {
        fprintf(stderr, "TX: failed to open input file: %s\n", file_path);
        return false;
    }

    size_t filesize = tx->myio->size(tx->myio);
    if (filesize > 16777215)
    {
        fprintf(stderr, "TX: file too large (>16MB): %s\n", file_path);
        tx_session_reset(tx);
        return false;
    }

    tx->rq = nanorq_encoder_new(filesize, ctx->symbol_size, 1);
    if (!tx->rq)
    {
        fprintf(stderr, "TX: failed to create RaptorQ encoder for: %s\n", file_path);
        tx_session_reset(tx);
        return false;
    }
    nanorq_set_max_esi(tx->rq, MAX_ESI);

    tx->num_sbn = nanorq_blocks(tx->rq);
    tx->esi = (uint32_t *)calloc((size_t)tx->num_sbn, sizeof(uint32_t));
    if (!tx->esi)
    {
        fprintf(stderr, "TX: failed to allocate ESI counters\n");
        tx_session_reset(tx);
        return false;
    }

    for (int b = 0; b < tx->num_sbn; b++) nanorq_generate_symbols(tx->rq, b, tx->myio);

    uint8_t config_packet[CONFIG_PACKET_SIZE] = {0};
    nanorq_oti_common_reduced(tx->rq, config_packet + 1);          // 5 bytes
    nanorq_oti_scheme_specific_align1(tx->rq, config_packet + 6);  // 3 bytes
    memcpy(tx->config_body, config_packet + 1, CONFIG_BODY_SIZE);

    strncpy(tx->file_path, file_path, sizeof(tx->file_path) - 1);
    tx->mtime = mtime;
    tx->frames_limit = parse_frames_limit_from_filename(file_path);
    tx->frames_sent = 0;
    tx->next_sbn = 0;
    tx->active = true;

    fprintf(stdout, "TX: loaded file %s (frames_limit=%lld, symbol_size=%u, blocks=%d)\n",
            tx->file_path, (long long)tx->frames_limit, ctx->symbol_size, tx->num_sbn);
    return true;
}

static bool tx_send_one_frame(daemon_ctx_t *ctx, tx_session_t *tx)
{
    uint8_t frame[ctx->frame_size];
    uint8_t symbol[ctx->symbol_size];

    int sbn = tx->next_sbn++ % tx->num_sbn;
    uint32_t esi = tx->esi[sbn];
    if (esi > MAX_ESI)
    {
        tx->esi[sbn] = 0;
        esi = 0;
    }

    uint64_t written = nanorq_encode(tx->rq, symbol, esi, (uint8_t)sbn, tx->myio);
    if (written != ctx->symbol_size)
    {
        fprintf(stderr, "TX: nanorq_encode failed (sbn=%d esi=%u)\n", sbn, esi);
        return false;
    }
    tx->esi[sbn]++;

    memset(frame, 0, sizeof(frame));
    memcpy(frame + 1, tx->config_body, CONFIG_BODY_SIZE);
    nanorq_tag_reduced((uint8_t)sbn, esi, frame + 1 + CONFIG_BODY_SIZE);
    memcpy(frame + 1 + CONFIG_BODY_SIZE + TAG_BODY_SIZE, symbol, ctx->symbol_size);

    frame[0] = (PACKET_RQ_CONFIG << 6) & 0xff;
    frame[0] |= crc6_0X6F(1, frame + HERMES_SIZE, (int)ctx->frame_size - HERMES_SIZE);

    if (tcp_interface_send_kiss(&ctx->tcp_iface, frame, ctx->frame_size) < 0)
    {
        fprintf(stderr, "TX: failed to send frame to modem\n");
        return false;
    }

    tx->frames_sent++;
    if (ctx->verbose && (tx->frames_sent % 100) == 0)
    {
        fprintf(stdout, "TX: sent=%lld file=%s\n", (long long)tx->frames_sent, tx->file_path);
    }
    return true;
}

static uint64_t parse_oti_common_from_frame(const uint8_t *frame)
{
    uint64_t oti_common = 0;
    oti_common |= (uint64_t)(frame[1] & 0xff) << 24;
    oti_common |= (uint64_t)(frame[2] & 0xff) << 32;
    oti_common |= (uint64_t)(frame[3] & 0xff) << 40;
    oti_common |= (uint64_t)(frame[4] & 0xff);
    oti_common |= (uint64_t)(frame[5] & 0xff) << 8;
    return oti_common;
}

static uint32_t parse_oti_scheme_from_frame(const uint8_t *frame)
{
    uint32_t oti_scheme = 0;
    oti_scheme |= (frame[6] & 0xff) << 24;
    oti_scheme |= (frame[7] & 0xff) << 8;
    oti_scheme |= (frame[8] & 0xff) << 16;
    oti_scheme |= 1;
    return oti_scheme;
}

static bool rx_session_start(daemon_ctx_t *ctx, rx_session_t *rx, uint64_t oti_common, uint32_t oti_scheme)
{
    rx_session_reset(rx);

    if (!build_output_path(ctx->rx_dir, rx->out_path, sizeof(rx->out_path)))
    {
        fprintf(stderr, "RX: failed to create output file path\n");
        return false;
    }

    rx->myio = ioctx_from_file(rx->out_path, 0);
    if (!rx->myio)
    {
        fprintf(stderr, "RX: failed to open output file: %s\n", rx->out_path);
        return false;
    }

    rx->rq = nanorq_decoder_new(oti_common, oti_scheme);
    if (!rx->rq)
    {
        fprintf(stderr, "RX: failed to create decoder\n");
        rx_session_reset(rx);
        return false;
    }
    nanorq_set_max_esi(rx->rq, MAX_ESI);

    rx->num_sbn = nanorq_blocks(rx->rq);
    rx->block_decoded = (bool *)calloc((size_t)rx->num_sbn, sizeof(bool));
    rx->block_symbols_seen = (uint32_t *)calloc((size_t)rx->num_sbn, sizeof(uint32_t));
    if (!rx->block_decoded || !rx->block_symbols_seen)
    {
        fprintf(stderr, "RX: allocation failed for decoder state\n");
        rx_session_reset(rx);
        return false;
    }

    rx->oti_common = oti_common;
    rx->oti_scheme = oti_scheme;
    rx->active = true;

    fprintf(stdout, "RX: new session -> %s (blocks=%d)\n", rx->out_path, rx->num_sbn);
    return true;
}

static bool rx_session_is_complete(rx_session_t *rx)
{
    for (int i = 0; i < rx->num_sbn; i++)
    {
        if (!rx->block_decoded[i]) return false;
    }
    return true;
}

static void *tx_thread_main(void *arg)
{
    daemon_ctx_t *ctx = (daemon_ctx_t *)arg;
    tx_session_t tx = {0};

    while (running)
    {
        if (tx.active)
        {
            struct stat st;
            if (stat(tx.file_path, &st) != 0)
            {
                fprintf(stdout, "TX: file removed, stopping %s\n", tx.file_path);
                tx_session_reset(&tx);
                continue;
            }
            if (st.st_mtime != tx.mtime)
            {
                fprintf(stdout, "TX: file changed, reloading %s\n", tx.file_path);
                tx_session_open(ctx, &tx, tx.file_path, st.st_mtime);
                continue;
            }
        }

        if (!tx.active)
        {
            char file_path[PATH_MAX];
            time_t mtime = 0;
            if (!find_first_regular_file(ctx->tx_dir, file_path, sizeof(file_path), &mtime))
            {
                usleep(200000);
                continue;
            }
            if (!tx_session_open(ctx, &tx, file_path, mtime))
            {
                usleep(500000);
                continue;
            }
        }

        if (tx.frames_limit != -1 && tx.frames_sent >= tx.frames_limit)
        {
            usleep(200000);
            continue;
        }

        if (!tx_send_one_frame(ctx, &tx))
        {
            running = 0;
            break;
        }
    }

    tx_session_reset(&tx);
    return NULL;
}

static void *rx_thread_main(void *arg)
{
    daemon_ctx_t *ctx = (daemon_ctx_t *)arg;
    rx_session_t rx = {0};
    uint64_t frames_rx = 0;
    uint64_t crc_errors = 0;

    uint8_t frame[MAX_PAYLOAD];
    while (running)
    {
        int frame_len = tcp_interface_recv_kiss(&ctx->tcp_iface, frame);
        if (frame_len == 0) continue;
        if (frame_len < 0)
        {
            fprintf(stderr, "RX: tcp read error/disconnect\n");
            running = 0;
            break;
        }

        frames_rx++;
        if ((uint32_t)frame_len != ctx->frame_size)
        {
            if (ctx->verbose)
                fprintf(stderr, "RX: dropping frame len=%d expected=%u\n", frame_len, ctx->frame_size);
            continue;
        }

        uint8_t packet_type = (frame[0] >> 6) & 0x3;
        if (packet_type == PACKET_RQ_PAYLOAD)
        {
            if (ctx->verbose) fprintf(stdout, "RX: side-info packet (0x03) len=%d\n", frame_len);
            continue;
        }
        if (packet_type != PACKET_RQ_CONFIG) continue; // v2 data path is 0x02

        uint8_t crc_local = frame[0] & 0x3f;
        uint8_t crc_calc = (uint8_t)crc6_0X6F(1, frame + HERMES_SIZE, (int)ctx->frame_size - HERMES_SIZE);
        if (crc_local != crc_calc)
        {
            crc_errors++;
            continue;
        }

        uint64_t oti_common = parse_oti_common_from_frame(frame);
        uint32_t oti_scheme = parse_oti_scheme_from_frame(frame);

        if (rx.completed_last &&
            rx.last_completed_oti_common == oti_common &&
            rx.last_completed_oti_scheme == oti_scheme &&
            !rx.active)
        {
            continue;
        }

        if (!rx.active ||
            rx.oti_common != oti_common ||
            rx.oti_scheme != oti_scheme)
        {
            if (!rx_session_start(ctx, &rx, oti_common, oti_scheme))
            {
                continue;
            }
            rx.completed_last = false;
        }

        uint8_t sbn = frame[1 + CONFIG_BODY_SIZE];
        uint32_t esi = (uint32_t)frame[1 + CONFIG_BODY_SIZE + 1] |
                       ((uint32_t)frame[1 + CONFIG_BODY_SIZE + 2] << 8);
        uint32_t tag = nanorq_tag(sbn, esi);

        int ret = nanorq_decoder_add_symbol(rx.rq,
                                            frame + 1 + CONFIG_BODY_SIZE + TAG_BODY_SIZE,
                                            tag,
                                            rx.myio);
        if (ret == NANORQ_SYM_ADDED)
        {
            rx.block_symbols_seen[sbn]++;
            if (!rx.block_decoded[sbn] &&
                rx.block_symbols_seen[sbn] >= nanorq_block_symbols(rx.rq, sbn))
            {
                if (nanorq_repair_block(rx.rq, rx.myio, sbn))
                {
                    rx.block_decoded[sbn] = true;
                    if (ctx->verbose) fprintf(stdout, "RX: block %u decoded\n", sbn);
                }
            }
        }

        if (rx_session_is_complete(&rx))
        {
            fprintf(stdout, "RX: FILE RECEIVED -> %s\n", rx.out_path);
            rx.completed_last = true;
            rx.last_completed_oti_common = rx.oti_common;
            rx.last_completed_oti_scheme = rx.oti_scheme;
            rx_session_reset(&rx);
        }

        if (ctx->verbose && (frames_rx % 200) == 0)
        {
            fprintf(stdout, "RX: frames=%llu crc_errors=%llu\n",
                    (unsigned long long)frames_rx,
                    (unsigned long long)crc_errors);
        }
    }

    rx_session_reset(&rx);
    return NULL;
}

static void print_usage(const char *prog)
{
    printf("Usage: %s [options]\n", prog);
    printf("  -m, --mode MODE      hermes-modem mode 0..6 (default: 0)\n");
    printf("  -t, --tx-dir DIR     TX queue directory (default: ./tx)\n");
    printf("  -r, --rx-dir DIR     RX output directory (default: ./rx)\n");
    printf("  -i, --ip IP          modem IP (default: 127.0.0.1)\n");
    printf("  -p, --port PORT      modem TCP port (default: 8100)\n");
    printf("  -v, --verbose        verbose logs\n");
    printf("  -h, --help           show help\n");
    printf("\n");
    printf("Filename frame budget: use suffix \"-N_frames\" (e.g. file-500_frames.bin).\n");
    printf("If suffix is absent, file is sent continuously until removed.\n");
}

int main(int argc, char *argv[])
{
    daemon_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.mode = 0;
    strncpy(ctx.tx_dir, "./tx", sizeof(ctx.tx_dir) - 1);
    strncpy(ctx.rx_dir, "./rx", sizeof(ctx.rx_dir) - 1);
    char ip[64];
    strncpy(ip, DEFAULT_MODEM_IP, sizeof(ip) - 1);
    int port = DEFAULT_MODEM_PORT;

    static struct option long_opts[] = {
        {"mode", required_argument, 0, 'm'},
        {"tx-dir", required_argument, 0, 't'},
        {"rx-dir", required_argument, 0, 'r'},
        {"ip", required_argument, 0, 'i'},
        {"port", required_argument, 0, 'p'},
        {"verbose", no_argument, 0, 'v'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "m:t:r:i:p:vh", long_opts, NULL)) != -1)
    {
        switch (opt)
        {
        case 'm': ctx.mode = atoi(optarg); break;
        case 't': strncpy(ctx.tx_dir, optarg, sizeof(ctx.tx_dir) - 1); break;
        case 'r': strncpy(ctx.rx_dir, optarg, sizeof(ctx.rx_dir) - 1); break;
        case 'i': strncpy(ip, optarg, sizeof(ip) - 1); break;
        case 'p': port = atoi(optarg); break;
        case 'v': ctx.verbose = true; break;
        case 'h':
            print_usage(argv[0]);
            return 0;
        default:
            print_usage(argv[0]);
            return 1;
        }
    }

    if (ctx.mode < 0 || ctx.mode > HERMES_MODE_MAX)
    {
        fprintf(stderr, "Invalid mode: %d\n", ctx.mode);
        return 1;
    }

    ctx.frame_size = hermes_frame_size[ctx.mode];
    if (ctx.frame_size <= (HERMES_SIZE + CONFIG_BODY_SIZE + TAG_BODY_SIZE))
    {
        fprintf(stderr, "Mode %d frame size too small for joint configuration+payload protocol\n", ctx.mode);
        return 1;
    }
    ctx.symbol_size = ctx.frame_size - HERMES_SIZE - CONFIG_BODY_SIZE - TAG_BODY_SIZE;

    mkdir(ctx.rx_dir, 0775);
    mkdir(ctx.tx_dir, 0775);

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    tcp_interface_init(&ctx.tcp_iface, ip, port);
    if (!tcp_interface_connect(&ctx.tcp_iface))
    {
        fprintf(stderr, "Failed to connect to hermes-modem at %s:%d\n", ip, port);
        return 1;
    }

    fprintf(stdout, "broadcast_daemon: mode=%d frame_size=%u symbol_size=%u tx_dir=%s rx_dir=%s\n",
            ctx.mode, ctx.frame_size, ctx.symbol_size, ctx.tx_dir, ctx.rx_dir);

    pthread_t tx_tid, rx_tid;
    pthread_create(&tx_tid, NULL, tx_thread_main, &ctx);
    pthread_create(&rx_tid, NULL, rx_thread_main, &ctx);

    while (running) sleep(1);

    if (ctx.tcp_iface.socket >= 0) shutdown(ctx.tcp_iface.socket, SHUT_RDWR);

    pthread_join(tx_tid, NULL);
    pthread_join(rx_tid, NULL);
    tcp_interface_disconnect(&ctx.tcp_iface);
    return 0;
}
