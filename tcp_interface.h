/* TCP interface for hermes-broadcast to hermes-modem communication
 *
 * Copyright (C) 2024 Rhizomatica
 * Author: Rafael Diniz <rafael@rhizomatica.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DEFAULT_MODEM_PORT 8100
#define DEFAULT_MODEM_IP "127.0.0.1"
#define TCP_BUFFER_SIZE 8192

// TCP interface handle
typedef struct {
    int socket;
    bool connected;
    bool shutdown;
    char ip[64];
    int port;
    pthread_mutex_t tx_mutex;
} tcp_interface_t;

// Initialize TCP interface structure
void tcp_interface_init(tcp_interface_t *iface, const char *ip, int port);

// Connect to hermes-modem
bool tcp_interface_connect(tcp_interface_t *iface);

// Disconnect from hermes-modem
void tcp_interface_disconnect(tcp_interface_t *iface);

// Send data with KISS framing to hermes-modem (thread-safe)
// Returns number of bytes sent on success, -1 on error
int tcp_interface_send_kiss(tcp_interface_t *iface, uint8_t *data, size_t len);

// Receive data with KISS framing from hermes-modem
// Returns frame length when complete frame received, 0 if no complete frame, -1 on error
// frame_buffer should be at least MAX_PAYLOAD bytes
int tcp_interface_recv_kiss(tcp_interface_t *iface, uint8_t *frame_buffer);

// Check if connected
bool tcp_interface_is_connected(tcp_interface_t *iface);

#ifdef __cplusplus
};
#endif
