#!/usr/bin/env python3
"""Host regression for matrix parsing, register isolation and PCM statistics.

Compile the actual driver helpers with MMIO stubs; this does not prove
microphone hardware, clock timing, filesystem commits or audible content.
"""
import os
import re
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DRIVER = ROOT / 'board/a733-cubie-a7z/openvela-overlay/vendor/allwinnertech/chips/a733/a733_i2s0_audio.c'
source = DRIVER.read_text()


def function(name):
    start = source.index('static ', source.index(name) - 20)
    opening = source.index('\n{', start)
    closing = source.index('\n}', opening)
    return source[start:closing + 2]


names = ('a733_parse_capturecfg', 'a733_pb_pull',
         'a733_i2s0_set_clock_polarity', 'a733_i2s0_capture_stats',
         'a733_i2s0_read')
# Use production constants and diagnostic layout, rather than a parallel copy.
constants = '\n'.join(line for line in source.splitlines()
                      if re.match(r'#define (I2S_FMT0_[BL].*POLARITY|I2S_FMT0 |A733_I2S0_BASE |A733_CAPTURE_PATH |A733_CAPTURE_RAW_PATH )', line))
structure = re.search(r'struct a733_i2s0_diag_s\n\{.*?\n\};', source, re.S)[0]
prefix = r'''
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#define OK 0
typedef int mutex_t;
struct file { off_t f_pos; };
static int nxmutex_lock(mutex_t *lock) { (void)lock; return 0; }
static void nxmutex_unlock(mutex_t *lock) { (void)lock; }
static void a733_i2s0_probe(void) {}
#define A733_PB_BASE 0x1000u
static uint32_t fmt0_register;
static uint32_t pull_register;
static uint32_t getreg32(uintptr_t address);
static void putreg32(uint32_t value, uintptr_t address);
'''
mmio = r'''
static struct a733_i2s0_diag_s g_i2s0;
static uint32_t getreg32(uintptr_t address)
{
  if (address == A733_I2S0_BASE + I2S_FMT0) return fmt0_register;
  assert(address == A733_PB_BASE + 0x30);
  return pull_register;
}
static void putreg32(uint32_t value, uintptr_t address)
{
  if (address == A733_I2S0_BASE + I2S_FMT0) fmt0_register = value;
  else {
    assert(address == A733_PB_BASE + 0x30);
    pull_register = value;
  }
}
'''
tests = r'''
int main(void)
{
  unsigned int cfg[4];
  char text[64];
  const char *invalid[] = {
    "", "0,0,2", "0,0,2,", "0,0,2,3000,1", "-1,0,2,3000",
    "+0,0,2,3000", "0, 0,2,3000", "0,0,2,3000x",
    "2,0,2,3000", "0,2,2,3000", "0,0,1,3000", "0,0,3,3000",
    "0,0,2,99", "0,0,2,10001", "0,0,2,4294967296",
    "999999999999999999999999,0,2,3000"
  };
  for (unsigned int slot = 0; slot < 2; slot++)
    for (unsigned int edge = 0; edge < 2; edge++)
      for (unsigned int pull = 0; pull <= 2; pull += 2) {
        snprintf(text, sizeof(text), "%u,%u,%u,3000", slot, edge, pull);
        assert(a733_parse_capturecfg(text, cfg) == 0);
        assert(cfg[0] == slot && cfg[1] == edge && cfg[2] == pull && cfg[3] == 3000);
      }
  assert(a733_parse_capturecfg("0,0,0,100", cfg) == 0);
  assert(a733_parse_capturecfg("1,1,2,10000", cfg) == 0);
  for (unsigned int i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++)
    assert(a733_parse_capturecfg(invalid[i], cfg) < 0);

  fmt0_register = 0xa5a5a5a5u;
  uint32_t unchanged = fmt0_register & ~(I2S_FMT0_BCLK_POLARITY | I2S_FMT0_LRCK_POLARITY);
  a733_i2s0_set_clock_polarity(1);
  assert(fmt0_register == (unchanged | I2S_FMT0_BCLK_POLARITY));
  a733_i2s0_set_clock_polarity(0);
  assert(fmt0_register == unchanged);
  pull_register = 0x12345678u;
  unchanged = pull_register & ~(3u << 16);
  a733_pb_pull(8, 2);
  assert(pull_register == (unchanged | (2u << 16)));
  a733_pb_pull(8, 0);
  assert(pull_register == unchanged);

  const int16_t pcm[] = {0, 32767, -32768, -1, 0, 1};
  const uint32_t raw[] = {0, 0x007fff55, 0xff8000aa, 0xffffff99, 0, 0x00000166};
  for (unsigned int mode = 0; mode < 2; mode++) {
    a733_i2s0_capture_stats(mode ? (const void *)raw : (const void *)pcm, 6, mode);
    assert(g_i2s0.capture_min == -32768 && g_i2s0.capture_max == 32767);
    assert(g_i2s0.capture_nonzero == 4 && g_i2s0.capture_zero == 2);
    assert(g_i2s0.capture_sign_transitions == 2);
    assert(g_i2s0.capture_sum_square == UINT64_C(2147418115));
  }
  int16_t *full_scale = malloc(480000 * sizeof(int16_t));
  assert(full_scale);
  for (unsigned int i = 0; i < 480000; i++) full_scale[i] = -32768;
  a733_i2s0_capture_stats(full_scale, 480000, false);
  assert(g_i2s0.capture_sum_square == UINT64_C(515396075520000));
  assert(g_i2s0.capture_zero == 0 && g_i2s0.capture_sign_transitions == 0);
  free(full_scale);
  strcpy(g_i2s0.last_capture_path, "/data/inmp441-right-invert-none.wav");
  struct file file = {0};
  char report[4096] = {0};
  ssize_t length = a733_i2s0_read(&file, report, sizeof(report) - 1);
  assert(length > 0 && length < 3583);
  assert(strstr(report, "sum-square=515396075520000"));
  assert(strstr(report, "/data/inmp441-right-invert-none.wav"));
  assert(strstr(report, "matrix: capturecfg="));
  assert(strstr(report, "(sign-extended S24 FIFO words)\n"));
  assert(a733_i2s0_read(&file, report, sizeof(report)) == 0);
  file.f_pos = 0;
  char chunked[4096] = {0};
  size_t offset = 0;
  ssize_t chunk;
  while ((chunk = a733_i2s0_read(&file, chunked + offset, 17)) > 0)
    offset += chunk;
  assert(offset == (size_t)length && strcmp(report, chunked) == 0);
  a733_i2s0_capture_stats(NULL, 0, false);
  assert(g_i2s0.capture_min == 0 && g_i2s0.capture_max == 0);
  assert(g_i2s0.capture_nonzero == 0 && g_i2s0.capture_sum_square == 0);
  puts("PASS: 8 configs, boundaries, 16 malformed inputs, MMIO isolation/reset, PCM/raw statistics, 64-bit full-scale sum, report formatting/chunking");
}
'''
with tempfile.TemporaryDirectory(prefix='a733-i2s-matrix-') as directory:
    cfile = Path(directory) / 'test.c'
    binary = Path(directory) / 'test'
    cfile.write_text('\n'.join((prefix, constants, structure, mmio,
                               *(function(name) for name in names), tests)))
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra',
                    '-Werror', '-fsanitize=undefined', '-o', str(binary), str(cfile)], check=True)
    subprocess.run([str(binary)], check=True)
