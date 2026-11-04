/* SPDX-License-Identifier: Apache-2.0 */
#ifndef AIPET_HTTP_FRAMING_H
#define AIPET_HTTP_FRAMING_H
#include <stddef.h>
#include <stdint.h>

/* Encoded chunked body only: 1 complete, 0 incomplete, -1 malformed.
 * Never search payload for "0\r\n\r\n"; it may be ordinary chunk data.
 * The final zero chunk includes the complete trailer section. */
static int pet_chunked_complete(const char *data, size_t len, size_t *end)
{
    size_t pos = 0;
    for (;;) {
        size_t size = 0, digits = 0;
        while (pos < len) {
            unsigned char ch = (unsigned char)data[pos];
            unsigned int digit;
            if (ch >= '0' && ch <= '9') digit = ch - '0';
            else if (ch >= 'a' && ch <= 'f') digit = ch - 'a' + 10;
            else if (ch >= 'A' && ch <= 'F') digit = ch - 'A' + 10;
            else break;
            if (size > (SIZE_MAX - digit) / 16) return -1;
            size = size * 16 + digit;
            ++digits; ++pos;
        }
        if (pos == len) return 0;
        if (!digits) return -1;
        if (data[pos] == ';') {
            while (pos < len && data[pos] != '\r') {
                unsigned char ch = (unsigned char)data[pos++];
                if (ch < 32 || ch == 127) return -1;
            }
        }
        if (pos == len) return 0;
        if (data[pos++] != '\r') return -1;
        if (pos == len) return 0;
        if (data[pos++] != '\n') return -1;
        if (!size) {
            for (;;) {
                size_t begin = pos;
                int colon = 0;
                while (pos < len && data[pos] != '\r') {
                    unsigned char ch = (unsigned char)data[pos++];
                    if (ch == ':') colon = 1;
                    if ((ch < 32 && ch != '\t') || ch == 127) return -1;
                }
                if (pos == len) return 0;
                ++pos;
                if (pos == len) return 0;
                if (data[pos++] != '\n') return -1;
                if (pos == begin + 2) { *end = pos; return 1; }
                if (!colon) return -1;
            }
        }
        if (size > len - pos) return 0;
        pos += size;
        if (pos == len) return 0;
        if (data[pos++] != '\r') return -1;
        if (pos == len) return 0;
        if (data[pos++] != '\n') return -1;
    }
}
#endif
