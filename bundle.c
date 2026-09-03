/* HERMES broadcast bundle parser.  See bundle.h. */

#include "bundle.h"

#include <string.h>

int bundle_parse(const uint8_t *buf, size_t len,
                 char *name, size_t namelen,
                 const uint8_t **payload, size_t *payload_len)
{
    if (!buf || len < 6)   /* size field + at least one name byte + '\n' */
        return -1;

    uint32_t body = (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) |
                    ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
    if ((size_t)body + 4 != len)
        return -1;               /* truncated or over-long: not our bundle */

    /* The name runs to the first '\n'.  Bounded by the buffer, so a bundle
     * with no terminator is rejected rather than read off the end. */
    size_t n = 0;
    while (4 + n < len && buf[4 + n] != '\n')
        n++;
    if (4 + n >= len || n == 0 || n > BUNDLE_NAME_MAX)
        return -1;

    /* Refuse anything that is not a bare filename: a receiver must never be
     * talked into writing outside its own directory by whatever the sender
     * put in the name field. */
    for (size_t i = 0; i < n; i++)
        if (buf[4 + i] == '/' || buf[4 + i] == '\\' || buf[4 + i] == 0)
            return -1;
    if (buf[4] == '.' && (n == 1 || (n == 2 && buf[5] == '.')))
        return -1;               /* "." and ".." */

    if (name && namelen)
    {
        if (n + 1 > namelen) return -1;
        memcpy(name, buf + 4, n);
        name[n] = 0;
    }
    if (payload)     *payload = buf + 4 + n + 1;
    if (payload_len) *payload_len = len - (4 + n + 1);
    return 0;
}
