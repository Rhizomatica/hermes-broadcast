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

## Shared Memory Mode (Mercury modem)

Use Mercury modem in operating mode TX_SHM for the transmitter and RX_SHM for the receiver. The arguments of both programs are a file (to transmit or receive) and the modulation mode (0 to 16).

```
$ ./transmitter file_to_transmit 10
$ ./receiver file_to_receive 10
```

## TCP Mode (mercury)

For use with mercury, enable TCP mode with the `-t` flag. Data is sent/received using KISS framing over TCP (default port 8100).

### Transmitter

```
$ ./transmitter -t file_to_transmit 0
$ ./transmitter -t -i 192.168.1.100 -p 8100 file_to_transmit 1
```

### Receiver

```
$ ./receiver -t file_to_receive 0
$ ./receiver -t -i 192.168.1.100 -p 8100 file_to_receive 1
```

### Command Line Options

```
  -t, --tcp         Use TCP connection to mercury (default: shared memory)
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
  -m, --mode MODE      mercury mode (0..6, default 1)
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

### Mercury Modem (Shared Memory Mode)

Valid modulation modes range from 0 to 16 (inclusive).

### Mercury (TCP Mode)

Valid modulation modes range from 0 to 6:

| Mode | Codec    | Payload Bytes |
|------|----------|---------------|
| 0    | DATAC1   | 510           |
| 1    | DATAC3   | 126           |
| 2    | DATAC0   | 14            |
| 3    | DATAC4   | 54            |
| 4    | DATAC13  | 14            |
| 5    | DATAC14  | 3             |
| 6    | FSK_LDPC | 30            |

# Architecture

## Shared Memory Mode

In shared memory mode, hermes-broadcast communicates with Mercury modem through POSIX shared memory (`/mercury-comm`). The transmitter writes encoded RaptorQ packets to shared memory, and the receiver reads decoded packets from shared memory.

## TCP Mode

In TCP mode, hermes-broadcast acts as a TCP client connecting to mercury's broadcast port. Data is framed using the KISS protocol (CMD_DATA = 0x02) for reliable packet delimiting over the TCP stream.

```
+------------------+       KISS/TCP        +---------------+
| hermes-broadcast | <------------------> | mercury       |
|  (transmitter/   |      port 8100       | (broadcast    |
|   receiver)      |                      |  interface)   |
+------------------+                      +---------------+
```

# About

Author: Rafael Diniz
