# hermes-broadcast

High-frequency Emergency and Rural Multimedia Exchange System (HERMES) software for data broadcast over HF. Supports Mercury (https://github.com/Rhizomatica/mercury) modem.

Data carousel mechanism implemented using RaptorQ fountain code (NanoRQ
implementation: https://github.com/sleepybishop/nanorq/).

**The wire format is specified byte-exactly in [WIRE-FORMAT.md](WIRE-FORMAT.md)**,
which is kept identical in this repository and in Mercury. Read it before
changing any framing, tag or OTI encoding: deployed stations depend on it.

## Two frame formats

This repository contains two, and they do **not** interoperate with each other:

| binaries | format | notes |
|---|---|---|
| `broadcast_daemon` | **joint** — every frame carries the configuration | current; what Mercury implements |
| `transmitter` / `receiver` | **split** — a separate periodic configuration frame | legacy; kept working |

The joint format costs 8 more bytes per frame and is worth it on a broadcast
channel: there is no return path, so a receiver tunes in at an arbitrary moment
and with the split format can decode nothing at all until the next
configuration frame happens to come round. With the joint format it starts on
the first frame it hears.

New work should use the joint format.

## Interoperating with Mercury

Mercury implements the joint format natively, so a Mercury station and a
`broadcast_daemon` station exchange files in both directions. Mercury
additionally wraps the file in a small **bundle** that carries its original
filename (WIRE-FORMAT.md §7); this project does not, and names what it receives
`broadcast_<timestamp>.bin`. Neither side rejects the other's payload:

| sender | receiver | result |
|---|---|---|
| `broadcast_daemon` | Mercury | file saved as `broadcast_<timestamp>.bin` |
| Mercury | `broadcast_daemon` | file saved as `broadcast_<timestamp>.bin`, contents are the bundle |
| `transmitter` | `receiver` | works, legacy format |

Verified by running the real binaries at each end of two Mercury modems over a
simulated HF path.

**Both stations must use the same modulation mode.** There is no negotiation on
the broadcast plane.

## KISS command: declare what you are sending

Everything this project puts on Mercury's broadcast port is one modem frame, so
it is sent with KISS command **`0x03` (`CMD_MODEM_FRAME`)** — "already a modem
frame, transmit it untouched".

Mercury frames anything sent under another command as a *message*, prepending
its own header and a 2-byte length prefix and truncating the payload by 3 bytes
to make room. For a modem frame that is fatal: the far side receives 3 bytes
short and discards it.

Mercury used to infer this from the payload's first byte, and that inference has
been removed — it could not be made correct, because those bytes belong to the
sender. See [WIRE-FORMAT.md](WIRE-FORMAT.md) §1.

> **If you are updating one side, update both.** Builds of this project older
> than this change send `CMD_DATA`, which a current Mercury treats as a message.
> `transmitter`, `receiver` and `broadcast_daemon` all now send `0x03`.

## Tests

```
make test        # RaptorQ round-trip through the reduced OTI/tag path
make test-e2e    # the real transmitter and receiver over a loopback relay
```

`make test` matters most after re-vendoring nanorq: the vendored copy carries
three HERMES-local helpers (`nanorq_tag_reduced`, `nanorq_oti_common_reduced`,
`nanorq_oti_scheme_specific_align1`) that upstream does not have, and losing
them would not necessarily fail a build — a subtly different byte layout
encodes fine and simply never decodes on the far side. The test drives the real
wire layout through encode, loss and decode and requires a byte-identical file.

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
