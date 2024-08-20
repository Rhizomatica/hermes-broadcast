# hermes-broadcast

High-frequency Emergency and Rural Multimedia Exchange System (HERMES) software for data broadcast over HF. At this point Mercury (https://github.com/Rhizomatica/mercury) modem is supported.

Data carousel mechanism implemented using RaptorQ fountain code (NanoRQ implementation: https://github.com/sleepybishop/nanorq/).

# Compilation

Code is implemented in C99 C language and needs a compliant POSIX implementation. GCC is the compiler set in Makefile - edit it to your own needs.

For compiling, type:

```
$ make
```

Two binaries will be created: "transmitter" and "receiver".

# Usage

Use Mercury modem in operating mode TX_SHM for the transmitter and RX_SHM for the receiver. The arguments of both software is a file (to transmit or receive) and 
Mercury's modulation mode (0 to 16).

```
$ ./transmitter file_to_transmit mercury_modulation_mode
$ ./receiver file_to_receive mercury_modulation_mode
```

# About

Author: Rafael Diniz
