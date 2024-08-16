# hermes-broadcast

High-frequency Emergency and Rural Multimedia Exchange System (HERMES) software for broadcasting data through HF.

Use Mercury modem in MODE_SHM. Data carousel mechanism implemented using RaptorQ fountain code (NanoRQ).

# Compilation

For compiling a C99 C compiler is needed. GCC is the compiler set in Makefile - edit it to your needs. 

For compiling, type:

```
$ make
```

Two binaries will be created: "transmitter" and "receiver".

# Usage

The arguments of both software is a file (to transmit or receive) and Mercury's modulation mode (0 to 16).

```
$ ./transmitter file_to_transmit mercury_modulation_mode
$ ./receiver file_to_receive mercury_modulation_mode
```

# About

Author: Rafael Diniz
