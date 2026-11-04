/* SPDX-License-Identifier: Apache-2.0
 * Tests the actual extracted bttool_read_command implementation.
 * Line-editor hardware echo itself requires serial/SSH acceptance.
 */
#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#define CONFIG_SYSTEM_READLINE 1
#define CONFIG_NSH_LINELEN 128
#define PRINT(...) do { } while (0)
static const char *test_input;
static ssize_t mock_readline(char *buffer, int size)
{
    if (!test_input || !*test_input)
        return -1;
    size_t n = 0;
    while (n < (size_t)size - 1 && test_input[n]) {
        if (test_input[n++] == '\n')
            break;
    }
    memcpy(buffer, test_input, n);
    test_input += n;
    buffer[n] = 0;
    return n;
}
#define std_readline(b, s) mock_readline(b, s)
#include "bttool-console-extracted.h"

int main(void)
{
    char *buffer = NULL;
    size_t size = 0;
    test_input = "pair auto 0\r\n";
    assert(bttool_read_command(&buffer, &size) == 11);
    assert(!strcmp(buffer, "pair auto 0"));
    test_input = "scan start -t 0 -p 1M -m 0 -l 1\t \r\n";
    assert(bttool_read_command(&buffer, &size) > 0);
    assert(!strcmp(buffer, "scan start -t 0 -p 1M -m 0 -l 1"));
    test_input = "\r\n";
    assert(bttool_read_command(&buffer, &size) == 0);
    test_input = "\n";
    assert(bttool_read_command(&buffer, &size) == 0);
    test_input = NULL;
    assert(bttool_read_command(&buffer, &size) < 0);
    char oversized[CONFIG_NSH_LINELEN + 20];
    memset(oversized, 'x', sizeof(oversized) - 1);
    oversized[sizeof(oversized) - 1] = 0;
    test_input = oversized;
    assert(bttool_read_command(&buffer, &size) == 0);
    assert(buffer[0] == 0);
    assert(!*test_input);
    char tail[CONFIG_NSH_LINELEN + 20];
    memset(tail, 'x', CONFIG_NSH_LINELEN - 1);
    strcpy(tail + CONFIG_NSH_LINELEN - 1, "enable\nstate\n");
    test_input = tail;
    assert(bttool_read_command(&buffer, &size) == 0);
    assert(bttool_read_command(&buffer, &size) == 5);
    assert(!strcmp(buffer, "state")); /* discarded tail did not execute */
    char boundary[CONFIG_NSH_LINELEN];
    memset(boundary, 'x', sizeof(boundary) - 2);
    boundary[sizeof(boundary) - 2] = '\n';
    boundary[sizeof(boundary) - 1] = 0;
    test_input = boundary;
    assert(bttool_read_command(&buffer, &size) == CONFIG_NSH_LINELEN - 2);
    free(buffer);
    buffer = NULL;
    test_input = "quit\r\n";
    assert(bttool_read_command(&buffer, &size) == 4);
    assert(!strcmp(buffer, "quit"));
    free(buffer);
    puts("bttool console normalization/EOF/limit tests: PASS");
    return 0;
}
