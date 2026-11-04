#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Command-model ONLY: never executes U-Boot, I2C or block devices.
set -u
declare -A mem
record=40082000
ready=40082008
scratch=4008200c
values=$((16#40082010))
mem[$record]=$((16#41375057));mem[40082004]=$((16#19201))
reads=0;commits=0;fdt=0;selected=0
mode_loaded=0
fail=${FAIL:-none}
mem[40084000]=$((16#41374d44));mem[40084004]=1
[[ $fail != mode-signature ]] || mem[40084000]=0
[[ $fail != mode-abi ]] || mem[40084004]=0
ext4load() {
  [[ "$1 $2" == 'mmc 0:3' ]] || return 11
  case "$4" in
    /boot/openvela/a733/Image)
      [[ $3 == 40080000 && $fail != load ]] || return 1
      filesize=700000;[[ $fail != size ]] || filesize=1
      return 0;;
    /boot/dtb/sun60i-a733-cubie-a7z.dtb)
      [[ $3 == 4fa00000 && $fail != fdt ]] || return 1
      fdt=1;return 0;;
    /boot/openvela/a733/imx214-1080p.a7cm)
      [[ ${RELEASE:-192} -ge 194 && "$3 $5" == '40084010 294' && $fail != mode-read ]] || return 1
      filesize=294;[[ $fail != mode-size ]] || filesize=1
      mode_loaded=1;return 0;;
    *) return 12;;
  esac
}
crc32() {
  [[ "$1 $2 $3" == "40080000 700000 $scratch" && $fail != crc-command ]] || return 1
  mem[$scratch]=$((16#01234567))
  [[ $fail != endian ]] || mem[$scratch]=$((16#67452301))
  [[ $fail != crc ]] || mem[$scratch]=123
  return 0
}
itest.l() {
  local lhs=$1 rhs=$3
  if [[ $lhs == \** ]]; then lhs=${lhs#\*};lhs=${mem[$lhs]:-0};else lhs=$((16#$lhs));fi
  [[ $2 == == && $lhs -eq $((16#$rhs)) ]]
}
itest.b() { itest.l "$@"; }
mw.l() {
  if [[ ${RELEASE:-192} -ge 194 && $1 == 40084008 ]];then
    [[ $2 == 294 && $3 == 1 && $mode_loaded == 1 && $fail != mode-bytes ]] || return 1
    mem[$1]=$((16#$2));return 0
  fi
  if [[ ${RELEASE:-192} -ge 194 && $1 == 4008400c ]];then
    [[ $2 == 434d4f31 && $3 == 1 && ${mem[40084008]:-0} == 660 && $fail != mode-ready ]] || return 1
    mem[$1]=$((16#$2));return 0
  fi
  [[ $1 == "$ready" && $3 == 1 ]] || return 15
  if [[ $2 == 0 ]];then
    [[ $reads == 0 && $commits == 0 ]] || return 16
  else
    [[ $2 == 434d5031 && $reads == 6 && $fdt == 1 && $fail != commit ]] || return 1
    commits=1
  fi
  mem[$ready]=$((16#$2));return 0
}
i2c() {
  if [[ "$1 $2" == 'dev 0' && $fail != dev ]]; then selected=1;return 0;fi
  [[ $1 == read && $2 == 36 && $4 == 1 && $selected == 1 ]] || return 1
  local regs=(0e.1 0f.1 10.1 12.1 20.1 2a.1)
  local raw=(3 0 255 23 193 13)
  [[ $reads -lt 6 && $3 == "${regs[$reads]}" && $5 == $(printf %x "$((values+reads))") ]] || return 18
  [[ $fail != read$reads ]] || return 1
  [[ $fail != bad-id || $reads != 0 ]] || raw[0]=0
  [[ $fail != bad-version || $reads != 1 ]] || raw[1]=1
  mem[$5]=${raw[$reads]};((reads+=1));return 0
}
md.b() { [[ "$1 $2" == '40082010 6' ]]; }
booti() {
  [[ "$*" == '40080000 - 4fa00000' && $reads == 6 && $commits == 1 && $fdt == 1 ]] || return 20
  if [[ ${RELEASE:-192} -ge 194 ]];then
    [[ $mode_loaded == 1 && ${mem[4008400c]:-0} == $((16#434d4f31)) ]] || return 21
  fi
  echo MOCK_BOOTI_REACHED;return 0
}
source "$1"
