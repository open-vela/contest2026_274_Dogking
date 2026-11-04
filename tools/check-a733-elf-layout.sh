#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Check linker/MMU/booti contracts without claiming hardware boot success.
set -euo pipefail
elf=${1:?unstripped ELF required}
binary=${2:-}
nm_tool=${A733_NM:-aarch64-none-elf-nm}
readelf_tool=${A733_READELF:-aarch64-none-elf-readelf}
declare -A symbols
while read -r address kind name; do
  case "$name" in
    _image_start|_image_end|_image_file_end|_stext|_etext|_sztext|_srodata|_erodata|_szrodata|_sdata|_szdata|_sbss|_ebss|_s_initstack|_e_initstack|g_idle_topstack)
      symbols[$name]=$((16#$address));;
  esac
done < <("$nm_tool" -n --defined-only "$elf")

fail() { echo "a733-layout: FAIL: $*" >&2; exit 1; }
for name in _image_start _image_end _image_file_end _stext _etext _sztext _srodata _erodata _szrodata _sdata _szdata _sbss _ebss _s_initstack _e_initstack g_idle_topstack; do
  [[ -v symbols[$name] ]] || fail "missing $name"
done
for name in _stext _sztext _srodata _szrodata _sdata _szdata _s_initstack _e_initstack; do
  (( (symbols[$name] & 4095) == 0 )) || fail "$name is not 4KiB aligned"
done
(( symbols[_image_start] == 0x40200000 )) || fail "unexpected Image load address"
(( symbols[_etext] == symbols[_srodata] )) || fail "text/rodata boundary mismatch"
(( symbols[_erodata] == symbols[_sdata] )) || fail "rodata/data boundary mismatch"
(( symbols[_erodata] - symbols[_srodata] == symbols[_szrodata] )) || fail "rodata span mismatch"
(( symbols[_image_file_end] <= symbols[_sbss] && symbols[_sbss] <= symbols[_ebss] && symbols[_ebss] <= symbols[_s_initstack] )) || fail "data/BSS/stack ordering"
(( symbols[_e_initstack] == symbols[g_idle_topstack] && symbols[_e_initstack] == symbols[_image_end] )) || fail "stack/footprint boundary mismatch"
(( symbols[_sdata] + symbols[_szdata] == symbols[_e_initstack] )) || fail "RW region does not cover stacks"
(( symbols[_image_end] <= 0x48000000 )) || fail "BL31 carveout overlap"
arena_line=$("$nm_tool" -S --defined-only "$elf" | awk '$4 == "g_ufs_nop_arena" {print $1, $2, $3}')
if [[ -n "$arena_line" ]]; then
  read -r arena_address arena_size arena_kind <<< "$arena_line"
  arena_address=$((16#$arena_address))
  arena_size=$((16#$arena_size))
  [[ "$arena_kind" == b || "$arena_kind" == B ]] || fail "NOP arena is not private BSS"
  (( arena_size == 77824 && (arena_address & 4095) == 0 )) || fail "NOP arena size/alignment"
  (( arena_address >= symbols[_sbss] && arena_address + arena_size <= symbols[_ebss] &&
     arena_address >= 0x40200000 && arena_address + arena_size <= 0x48000000 )) || fail "NOP arena outside static stage1 BSS"
  echo "a733-layout: private NOP arena=$arena_address bytes=$arena_size; outside heap/stacks/BL31"
  nop_arena_start=$arena_address
  nop_arena_end=$((arena_address + arena_size))
elif [[ "${A733_UFS_DMA_NOP_EXPERIMENTAL:-0}" == 1 ]]; then
  fail "required private NOP arena missing"
fi
read_arena_line=$("$nm_tool" -S --defined-only "$elf" | awk '$4 == "g_ufs_read_arena" {print $1, $2, $3}')
if [[ -n "$read_arena_line" ]]; then
  [[ -v nop_arena_start ]] || fail "readcheck arena without NOP arena"
  read -r read_address read_size read_kind <<< "$read_arena_line"
  read_address=$((16#$read_address))
  read_size=$((16#$read_size))
  [[ "$read_kind" == b || "$read_kind" == B ]] || fail "readcheck arena is not BSS"
  (( read_size == 77824 && (read_address & 4095) == 0 &&
     read_address >= symbols[_sbss] && read_address + read_size <= symbols[_ebss] &&
     read_address >= 0x40200000 && read_address + read_size <= 0x48000000 )) || fail "readcheck arena size/alignment/bounds"
  (( read_address >= nop_arena_end || read_address + read_size <= nop_arena_start )) || fail "NOP/readcheck arenas overlap"
  echo "a733-layout: separate readcheck arena=$read_address bytes=$read_size; no overlap with NOP/heap/stacks/BL31"
elif [[ "${A733_UFS_READCHECK_EXPERIMENTAL:-0}" == 1 ]]; then
  fail "required private readcheck arena missing"
fi
bench_line=$("$nm_tool" -S --defined-only "$elf" | awk '$4 == "g_ufs_bench" {print $1, $2, $3}')
if [[ -n "$bench_line" ]]; then
  [[ -v read_address ]] || fail "benchmark buffer without readcheck arena"
  read -r bench_address bench_size bench_kind <<< "$bench_line"
  bench_address=$((16#$bench_address)); bench_size=$((16#$bench_size))
  [[ "$bench_kind" == b || "$bench_kind" == B ]] || fail "benchmark buffer is not BSS"
  (( bench_size == 65536 && (bench_address & 63) == 0 &&
     bench_address >= symbols[_sbss] && bench_address + bench_size <= symbols[_ebss] &&
     (bench_address + bench_size <= read_address || bench_address >= read_address + read_size) &&
     (bench_address + bench_size <= nop_arena_start || bench_address >= nop_arena_end) )) || fail "benchmark buffer bounds/overlap"
  echo "a733-layout: CPU-only benchmark buffer=$bench_address bytes=$bench_size; disjoint BSS"
elif [[ "${A733_UFS_BLOCK_RO_EXPERIMENTAL:-0}" == 1 ]]; then
  fail "required benchmark buffer missing"
fi
scratch_line=$("$nm_tool" -S --defined-only "$elf" | awk '$4 == "g_ufs_scratch" {print $1, $2, $3}')
if [[ -n "$scratch_line" ]]; then
  [[ -v bench_address && -v read_address ]] || fail "scratch without qualified buffers"
  read -r scratch_address scratch_size scratch_kind <<< "$scratch_line"
  scratch_address=$((16#$scratch_address)); scratch_size=$((16#$scratch_size))
  [[ "$scratch_kind" == b || "$scratch_kind" == B ]] || fail "scratch backup is not BSS"
  (( scratch_size >= 73728 && scratch_size < 77824 && (scratch_address & 63) == 0 &&
     scratch_address >= symbols[_sbss] && scratch_address + scratch_size <= symbols[_ebss] &&
     (scratch_address + scratch_size <= read_address || scratch_address >= read_address + read_size) &&
     (scratch_address + scratch_size <= nop_arena_start || scratch_address >= nop_arena_end) &&
     (scratch_address + scratch_size <= bench_address || scratch_address >= bench_address + bench_size) )) || fail "scratch backup bounds/overlap"
  echo "a733-layout: CPU-only scratch backup=$scratch_address bytes=$scratch_size; disjoint BSS, volatile"
elif [[ "${A733_UFS_SCRATCH_EXPERIMENTAL:-0}" == 1 ]]; then
  fail "required scratch RAM backup missing"
fi
data_line=$("$nm_tool" -S --defined-only "$elf" | awk '$4 == "g_ufs_data" {print $1, $2, $3}')
if [[ -n "$data_line" ]]; then
  [[ -v scratch_address ]] || fail "data state without scratch proof"
  read -r data_address data_size data_kind <<< "$data_line"
  data_address=$((16#$data_address)); data_size=$((16#$data_size))
  [[ "$data_kind" == b || "$data_kind" == B ]] || fail "data state is not BSS"
  (( data_size < 1024 && (data_address & 63) == 0 &&
     data_address >= symbols[_sbss] && data_address + data_size <= symbols[_ebss] &&
     (data_address + data_size <= read_address || data_address >= read_address + read_size) &&
     (data_address + data_size <= nop_arena_start || data_address >= nop_arena_end) &&
     (data_address + data_size <= bench_address || data_address >= bench_address + bench_size) &&
     (data_address + data_size <= scratch_address || data_address >= scratch_address + scratch_size) )) || fail "data state bounds/overlap"
  echo "a733-layout: CPU-only data state=$data_address bytes=$data_size; reuses qualified DMA arena"
elif [[ "${A733_UFS_DATA_EXPERIMENTAL:-0}" == 1 ]]; then
  fail "required data state missing"
fi
sections=$("$readelf_tool" -SW "$elf")
if [[ "$sections" == *" .z_init_"* || "$sections" == *" .z_devstate"* ]]; then
  fail "unplaced Zblue orphan section"
fi
if [[ "$sections" == *" .got"* ]]; then
  fail "unplaced GOT orphan section"
fi
if [[ -n "$binary" ]]; then
  test -f "$binary"
  file_bytes=$(stat -c %s "$binary")
  image_bytes=$(od -An -tu8 -j16 -N8 "$binary" | tr -d '[:space:]')
  magic=$(od -An -tx4 -j56 -N4 "$binary" | tr -d '[:space:]')
  [[ "$magic" == 644d5241 ]] || fail "ARM64 Image magic"
  (( file_bytes == symbols[_image_file_end] - symbols[_image_start] )) || fail "file-backed size mismatch"
  (( image_bytes == symbols[_image_end] - symbols[_image_start] && image_bytes >= file_bytes )) || fail "header does not reserve BSS/stacks"
  echo "a733-layout: file=$file_bytes runtime=$image_bytes bytes"
fi
echo "a733-layout: page alignment/segments/stack/Image footprint passed"
