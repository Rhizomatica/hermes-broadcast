# hermes-broadcast

High-frequency Emergency and Rural Multimedia Exchange System (HERMES) software for data broadcast over HF. Supports Mercury (https://github.com/Rhizomatica/mercury) modem.

Data carousel mechanism implemented using RaptorQ fountain code (NanoRQ implementation: https://github.com/sleepybishop/nanorq/).

# Compilation

Code is implemented in C99 C language and needs a compliant POSIX implementation. GCC is the compiler set in Makefile - edit it to your own needs.

For compiling, type:

```
$ make
```

Three binaries will be created: "transmitter", "receiver", and "broadcast_daemon".

# Usage

## TCP Mode

hermes-broadcast talks to Mercury over TCP, using KISS framing (default port
8100). The arguments of both programs are a file (to transmit or receive) and
the modulation mode.

The `-t` flag is still accepted so existing invocations keep working, but it is
now a no-op: TCP is the only transport. The shared-memory path has been
removed — it spoke to `/mercury-comm`, a segment only Mercury **v1** ever
created. Current Mercury has no broadcast SHM interface at all.

### Transmitter

```
$ ./transmitter file_to_transmit 0
$ ./transmitter -i 192.168.1.100 -p 8100 file_to_transmit 1
```

### Receiver

```
$ ./receiver file_to_receive 0
$ ./receiver -i 192.168.1.100 -p 8100 file_to_receive 1
```

### Command Line Options

```
  -t, --tcp         Accepted for compatibility (TCP is the only transport)
  -i, --ip IP       IP address of mercury (default: 127.0.0.1)
  -p, --port PORT   TCP port of mercury (default: 8100)
  -h, --help        Show help message
```

## Broadcast daemon (joint RaptorQ configuration and payload protocol)

`broadcast_daemon` runs TX and RX together over one TCP/KISS connection. It uses Mercury's broadcast header values (`0x03` for config/control-style frames and `0x04` for payload/side-information frames) and treats the low 5 header bits as a reserved extension field, not a CRC.

### Daemon usage

```
$ ./broadcast_daemon --mode 1 --tx-dir ./tx --rx-dir ./rx --ip 127.0.0.1 --port 8100
```

Options:

```
  -m, --mode MODE      mercury mode (0..10, default 1)
  -t, --tx-dir DIR     directory watched for files to transmit
  -r, --rx-dir DIR     directory where received files are written
  -i, --ip IP          mercury IP (default 127.0.0.1)
  -p, --port PORT      mercury broadcast port (default 8100)
  -v, --verbose        verbose logs
```

### Filename frame budget

To set a finite number of transmitted frames, include `-N_frames` in the filename.

- Example: `example-500_frames.bin` -> transmit 500 frames then stop.
- If suffix is absent, daemon transmits continuously until file is removed.

## Modulation Modes

Valid modulation modes range from 0 to 10. These are the indices Mercury's
own `mercury -l` reports, in its order:

| Mode | Codec    | Payload Bytes |
|------|----------|---------------|
| 0    | DATAC1   | 510           |
| 1    | DATAC3   | 126           |
| 2    | DATAC0   | 14            |
| 3    | DATAC4   | 54            |
| 4    | DATAC13  | 14            |
| 5    | DATAC14  | 3             | *(too small -- see below)* |
| 6    | FSK_LDPC | 30            |
| 7    | DATAC15  | 30            |
| 8    | DATAC16  | 14            |
| 9    | DATAC17  | 1180          |
| 10   | QAM16C2  | 1213          |

Modes 9 and 10 carry more than twice DATAC1's payload and are the ones to
reach for on a good channel; the low-payload modes (2, 4, 5, 8) are the robust
end. Both ends must agree on the mode — there is no negotiation on the
broadcast plane.

Mode 5 cannot carry broadcast at all: every frame needs a 4-byte RaptorQ tag
and the configuration packet is 9 bytes, so DATAC14's 3-byte frame leaves
nothing over. Both binaries reject it.

Modes 9 and 10 exceed the old 756-byte KISS payload cap, so they need a Mercury
build whose `MAX_PAYLOAD` covers the largest broadcast frame (1213). An older
Mercury rejects them with "Invalid broadcast frame size".

# Architecture

## TCP Mode

hermes-broadcast acts as a TCP client connecting to mercury's broadcast port. Data is framed using the KISS protocol (CMD_DATA = 0x02) for reliable packet delimiting over the TCP stream.

```
+------------------+       KISS/TCP        +---------------+
| hermes-broadcast | <------------------> | mercury       |
|  (transmitter/   |      port 8100       | (broadcast    |
|   receiver)      |                      |  interface)   |
+------------------+                      +---------------+
```

# About

Author: Rafael Diniz
