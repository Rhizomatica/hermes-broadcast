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

## Shared Memory Mode (Mercury v1 only)

> **Legacy.** The shared-memory path talks to `/mercury-comm`, which only
> Mercury **v1** creates — current Mercury (`mercuryv2`) uses a different
> segment and no longer offers `TX_SHM`/`RX_SHM` at all. Use TCP mode below
> with any current Mercury. The mode numbering here (0..16) is v1's, and does
> not match the modem's mode list either.

Use Mercury v1 in operating mode TX_SHM for the transmitter and RX_SHM for the receiver. The arguments of both programs are a file (to transmit or receive) and the modulation mode (0 to 16).

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
| 5    | DATAC14  | 3             |
| 6    | FSK_LDPC | 30            |
| 7    | DATAC15  | 30            |
| 8    | DATAC16  | 14            |
| 9    | DATAC17  | 1180          |
| 10   | QAM16C2  | 1213          |

Modes 9 and 10 carry more than twice DATAC1's payload and are the ones to
reach for on a good channel; the low-payload modes (2, 4, 5, 8) are the robust
end. Both ends must agree on the mode — there is no negotiation on the
broadcast plane.

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
