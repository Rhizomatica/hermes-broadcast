/* HERMES broadcast bundle: a file with its name inside it.
 *
 * Copyright (C) 2026 Rhizomatica
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * RaptorQ transfers an opaque object, so a filename has to travel inside it.
 * Mercury wraps what it sends this way; this project historically did not, and
 * named whatever arrived broadcast_<timestamp>.bin -- discarding a name that
 * was sitting right there in the payload.
 *
 *     [0..3]   uint32 little-endian: strlen(basename) + 1 + file bytes
 *     [4..]    basename, terminated by '\n' (not NUL)
 *     [...]    file contents
 *
 * Specified in WIRE-FORMAT.md §7.  Kept byte-identical with Mercury's
 * datalink_broadcast/bcast_file.c: two implementations of one on-air format
 * must not drift.
 */
#ifndef BUNDLE_H_
#define BUNDLE_H_

#include <stddef.h>
#include <stdint.h>

/** Longest basename we will accept. */
#define BUNDLE_NAME_MAX 255

/**
 * Parse a bundle.  @p payload points INTO @p buf; nothing is copied.
 *
 * A bundle arriving off the air is unauthenticated input from an unknown
 * station, so this refuses anything that could steer a write outside the
 * receive directory, and anything malformed enough to read off the end.
 *
 * @return 0 on success, -1 if @p buf is not a valid bundle.
 */
int bundle_parse(const uint8_t *buf, size_t len,
                 char *name, size_t namelen,
                 const uint8_t **payload, size_t *payload_len);

#endif /* BUNDLE_H_ */
