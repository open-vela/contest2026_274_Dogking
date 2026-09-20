/* SPDX-License-Identifier: Apache-2.0 */
#include "../board/a733-cubie-a7z/openvela-overlay/apps/system/aipet/pet_http_framing.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void fragmented(const char *s)
{
    size_t end = 0, len = strlen(s);
    for (size_t n = 0; n < len; ++n)
        assert(pet_chunked_complete(s, n, &end) == 0);
    assert(pet_chunked_complete(s, len, &end) == 1 && end == len);
}
int main(void)
{
    fragmented("0\r\n\r\n");
    fragmented("5\r\nhello\r\n0\r\n\r\n");
    fragmented("5;key=value\r\nhello\r\n0\r\nX-Test: yes\r\n\r\n");
    fragmented("5\r\n0\r\n\r\n\r\n0\r\n\r\n"); /* false terminator in payload */
    size_t end;
    const char *bad[] = {"z\r\n", "-1\r\n", "1\nx", "1\r\nx!", "0\r\nbad\r\n\r\n",
        "ffffffffffffffffffffffffffffffff\r\n"};
    for (size_t i = 0; i < sizeof(bad)/sizeof(bad[0]); ++i)
        assert(pet_chunked_complete(bad[i], strlen(bad[i]), &end) == -1);
    assert(pet_chunked_complete("0\r\n\r\nextra", 10, &end) == 1 && end == 5);
    puts("HTTP chunk framing fragment/trailer/malformed tests passed");
}
