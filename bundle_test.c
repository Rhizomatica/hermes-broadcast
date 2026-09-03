/* Tests for bundle.c
 *
 * Copyright (C) 2026 Rhizomatica
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A bundle arrives off an unauthenticated broadcast, so most of what matters
 * here is what the parser REFUSES.  The name field decides where the receiver
 * writes; anything that could steer that outside the receive directory has to
 * be rejected, and a malformed bundle must not be read off the end.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "bundle.h"

static int failures = 0;

static void check(int cond, const char *what)
{
    if (!cond) { printf("  FAIL: %s\n", what); failures++; }
}

/* Build a well-formed bundle, so each test only varies the one thing it is about. */
static size_t make(uint8_t *buf, const char *name, const char *body)
{
    size_t n = strlen(name), b = strlen(body);
    uint32_t sz = (uint32_t)(n + 1 + b);
    buf[0] = sz & 0xff; buf[1] = (sz >> 8) & 0xff;
    buf[2] = (sz >> 16) & 0xff; buf[3] = (sz >> 24) & 0xff;
    memcpy(buf + 4, name, n);
    buf[4 + n] = '\n';
    memcpy(buf + 4 + n + 1, body, b);
    return 4 + sz;
}

int main(void)
{
    uint8_t buf[512];
    char name[BUNDLE_NAME_MAX + 1];
    const uint8_t *payload;
    size_t plen;

    printf("bundle parser\n");

    /* --- the good case --- */
    size_t len = make(buf, "bulletin.txt", "hello there");
    check(bundle_parse(buf, len, name, sizeof(name), &payload, &plen) == 0,
          "a well-formed bundle parses");
    check(strcmp(name, "bulletin.txt") == 0, "the name comes back");
    check(plen == 11 && memcmp(payload, "hello there", 11) == 0,
          "the payload comes back");

    /* --- refusals that protect the filesystem --- */
    const char *hostile[] = { "../etc/passwd", "/etc/passwd", "..\\windows", "..", "." };
    for (unsigned i = 0; i < sizeof(hostile)/sizeof(hostile[0]); i++)
    {
        len = make(buf, hostile[i], "x");
        check(bundle_parse(buf, len, name, sizeof(name), NULL, NULL) == -1,
              hostile[i]);
    }

    /* --- refusals that protect the parser --- */
    check(bundle_parse(NULL, 32, name, sizeof(name), NULL, NULL) == -1, "NULL buffer");
    check(bundle_parse(buf, 3, name, sizeof(name), NULL, NULL) == -1, "too short to hold a header");

    len = make(buf, "a.bin", "payload");
    buf[0] = (uint8_t)(buf[0] + 7);          /* size field now disagrees */
    check(bundle_parse(buf, len, name, sizeof(name), NULL, NULL) == -1,
          "size field disagreeing with the object");

    memset(buf, 'A', sizeof(buf));            /* no '\n' anywhere */
    buf[0] = 12; buf[1] = buf[2] = buf[3] = 0;
    check(bundle_parse(buf, 16, name, sizeof(name), NULL, NULL) == -1,
          "no name terminator");

    len = make(buf, "x", "y");
    buf[4] = '\n';                            /* empty name */
    check(bundle_parse(buf, len, name, sizeof(name), NULL, NULL) == -1, "empty name");

    /* A plain file that is not a bundle at all must be refused, not
     * misparsed -- this is what broadcast_daemon's own transfers look like. */
    memset(buf, 0x5A, 64);
    check(bundle_parse(buf, 64, name, sizeof(name), NULL, NULL) == -1,
          "an un-bundled payload is not mistaken for one");

    printf(failures ? "FAILED\n" : "all bundle checks passed\n");
    return failures ? 1 : 0;
}
