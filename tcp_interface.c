/* TCP interface for hermes-broadcast to hermes-modem communication
 *
 * Copyright (C) 2024 Rhizomatica
 * Author: Rafael Diniz <rafael@rhizomatica.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "tcp_interface.h"
#include "kiss.h"

// Static KISS state for receiving
static kiss_state_t recv_kiss_state;
static uint8_t recv_partial_buffer[TCP_BUFFER_SIZE];
static size_t recv_partial_len = 0;

void tcp_interface_init(tcp_interface_t *iface, const char *ip, int port)
{
    memset(iface, 0, sizeof(tcp_interface_t));
    strncpy(iface->ip, ip ? ip : DEFAULT_MODEM_IP, sizeof(iface->ip) - 1);
    iface->port = port > 0 ? port : DEFAULT_MODEM_PORT;
    iface->socket = -1;
    iface->connected = false;
    iface->shutdown = false;
    pthread_mutex_init(&iface->tx_mutex, NULL);
    kiss_init(&recv_kiss_state);
}

bool tcp_interface_connect(tcp_interface_t *iface)
{
    if (iface->connected)
    {
        return true;
    }

    iface->socket = socket(AF_INET, SOCK_STREAM, 0);
    if (iface->socket < 0)
    {
        perror("tcp_interface: Failed to create TCP socket");
        return false;
    }

    struct sockaddr_in modem_addr;
    memset(&modem_addr, 0, sizeof(modem_addr));
    modem_addr.sin_family = AF_INET;
    modem_addr.sin_port = htons(iface->port);
    
    if (inet_pton(AF_INET, iface->ip, &modem_addr.sin_addr) <= 0)
    {
        perror("tcp_interface: Invalid IP address");
        close(iface->socket);
        iface->socket = -1;
        return false;
    }

    if (connect(iface->socket, (struct sockaddr *)&modem_addr, sizeof(modem_addr)) < 0)
    {
        perror("tcp_interface: Failed to connect to hermes-modem");
        close(iface->socket);
        iface->socket = -1;
        return false;
    }

    iface->connected = true;
    printf("tcp_interface: Connected to hermes-modem at %s:%d\n", iface->ip, iface->port);
    return true;
}

void tcp_interface_disconnect(tcp_interface_t *iface)
{
    iface->shutdown = true;
    if (iface->socket >= 0)
    {
        close(iface->socket);
        iface->socket = -1;
    }
    iface->connected = false;
    pthread_mutex_destroy(&iface->tx_mutex);
}

int tcp_interface_send_kiss(tcp_interface_t *iface, uint8_t *data, size_t len)
{
    if (!iface->connected || iface->socket < 0)
    {
        return -1;
    }

    // Allocate buffer for KISS frame (worst case: each byte doubled + framing)
    uint8_t *kiss_buffer = malloc(len * 2 + 3);
    if (!kiss_buffer)
    {
        return -1;
    }

    // Encode data with KISS framing
    int kiss_len = kiss_write_frame(data, len, kiss_buffer);

    pthread_mutex_lock(&iface->tx_mutex);
    
    ssize_t sent = send(iface->socket, kiss_buffer, kiss_len, 0);
    
    pthread_mutex_unlock(&iface->tx_mutex);
    
    free(kiss_buffer);

    if (sent < 0)
    {
        perror("tcp_interface: Error sending data");
        iface->connected = false;
        return -1;
    }

    if (sent < kiss_len)
    {
        fprintf(stderr, "tcp_interface: Partial send: %zd of %d bytes\n", sent, kiss_len);
    }

    return (int)sent;
}

int tcp_interface_recv_kiss(tcp_interface_t *iface, uint8_t *frame_buffer)
{
    if (!iface->connected || iface->socket < 0)
    {
        return -1;
    }

    // First, process any remaining data from previous recv
    for (size_t i = 0; i < recv_partial_len; i++)
    {
        int frame_len = kiss_read(&recv_kiss_state, recv_partial_buffer[i], frame_buffer);
        if (frame_len > 0)
        {
            // Move remaining data to beginning of buffer
            size_t remaining = recv_partial_len - i - 1;
            if (remaining > 0)
            {
                memmove(recv_partial_buffer, recv_partial_buffer + i + 1, remaining);
            }
            recv_partial_len = remaining;
            return frame_len;
        }
    }
    recv_partial_len = 0;

    // Receive more data from socket
    uint8_t recv_buffer[TCP_BUFFER_SIZE];
    ssize_t received = recv(iface->socket, recv_buffer, TCP_BUFFER_SIZE, 0);

    if (received == 0)
    {
        // Connection closed
        iface->connected = false;
        return -1;
    }
    else if (received < 0)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            return 0; // No data available
        }
        perror("tcp_interface: Error receiving data");
        iface->connected = false;
        return -1;
    }

    // Process received data through KISS decoder
    for (ssize_t i = 0; i < received; i++)
    {
        int frame_len = kiss_read(&recv_kiss_state, recv_buffer[i], frame_buffer);
        if (frame_len > 0)
        {
            // Save remaining data for next call
            recv_partial_len = received - i - 1;
            if (recv_partial_len > 0)
            {
                memcpy(recv_partial_buffer, recv_buffer + i + 1, recv_partial_len);
            }
            return frame_len;
        }
    }

    return 0; // No complete frame yet
}

bool tcp_interface_is_connected(tcp_interface_t *iface)
{
    return iface->connected;
}
