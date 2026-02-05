# hermes-broadcast

High-frequency Emergency and Rural Multimedia Exchange System (HERMES) software for data broadcast over HF. Supports Mercury (https://github.com/Rhizomatica/mercury) modem and hermes-modem.

Data carousel mechanism implemented using RaptorQ fountain code (NanoRQ implementation: https://github.com/sleepybishop/nanorq/).

# Compilation

Code is implemented in C99 C language and needs a compliant POSIX implementation. GCC is the compiler set in Makefile - edit it to your own needs.

For compiling, type:

```
$ make
```

Two binaries will be created: "transmitter" and "receiver".

# Usage

## Shared Memory Mode (Mercury modem)

Use Mercury modem in operating mode TX_SHM for the transmitter and RX_SHM for the receiver. The arguments of both software is a file (to transmit or receive) and Mercury's modulation mode (0 to 16).

```
$ ./transmitter file_to_transmit mercury_modulation_mode
$ ./receiver file_to_receive mercury_modulation_mode
```

## TCP Mode (hermes-modem)

For use with hermes-modem, enable TCP mode with the `-t` flag. Data is sent/received using KISS framing over TCP (default port 8100).

### Transmitter

```
$ ./transmitter -t file_to_transmit mercury_modulation_mode
$ ./transmitter -t -i 192.168.1.100 -p 8100 file_to_transmit mercury_modulation_mode
```

### Receiver

```
$ ./receiver -t file_to_receive mercury_modulation_mode
$ ./receiver -t -i 192.168.1.100 -p 8100 file_to_receive mercury_modulation_mode
```

### Command Line Options

```
  -t, --tcp         Use TCP connection to hermes-modem (default: shared memory)
  -i, --ip IP       IP address of hermes-modem (default: 127.0.0.1)
  -p, --port PORT   TCP port of hermes-modem (default: 8100)
  -h, --help        Show help message
```

## Mercury Modulation Modes

Valid modulation modes range from 0 to 16 (inclusive). Each mode corresponds to a specific frame size optimized for different channel conditions.

# Architecture

## Shared Memory Mode

In shared memory mode, hermes-broadcast communicates with Mercury modem through POSIX shared memory (`/mercury-comm`). The transmitter writes encoded RaptorQ packets to shared memory, and the receiver reads decoded packets from shared memory.

## TCP Mode

In TCP mode, hermes-broadcast acts as a TCP client connecting to hermes-modem's broadcast port. Data is framed using the KISS protocol (CMD_DATA = 0x02) for reliable packet delimiting over the TCP stream.

```
+------------------+       KISS/TCP        +---------------+
| hermes-broadcast | <------------------> | hermes-modem  |
|  (transmitter/   |      port 8100       | (broadcast    |
|   receiver)      |                      |  interface)   |
+------------------+                      +---------------+
```

# About

Author: Rafael Diniz
