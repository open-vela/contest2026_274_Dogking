#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Software command model ONLY: no device commands or real delays.
set -u
test_source=$1
source "$(dirname "$0")/test-a733-camera-handoff-v192.sh" /dev/null
ctrl=$((16#${CTRL:-17})); initial_ctrl=$ctrl
voltage=$((16#${VOLTAGE:-17}))
writes=0;before_reads=0;after_reads=0;voltage_reads=0;settled=0
trap 'printf "MODEL writes=%d before=%d after=%d voltage=%d ready=%d ctrl=%02x\n" "$writes" "$before_reads" "$after_reads" "$voltage_reads" "$commits" "$ctrl"' EXIT
i2c() {
  if [[ "$1 $2" == 'dev 0' ]];then
    [[ $fail != dev && ! ( $settled == 1 && $fail == post-dev ) ]] || return 1
    selected=1;return 0
  fi
  [[ $1 == read && $2 == 36 && $4 == 1 && $selected == 1 ]] || return 1
  if [[ "$3 $5" == '11.1 4008200c' ]];then
    [[ $reads == 6 && $fail != power-read-before ]] || return 1
    mem[$5]=$ctrl;((before_reads+=1));return 0
  elif [[ "$3 $5" == '11.1 4008200d' ]];then
    [[ $settled == 1 && $fail != power-read-after ]] || return 1
    [[ $fail != control-drift ]] || ctrl=$((16#17))
    mem[$5]=$ctrl;((after_reads+=1));return 0
  elif [[ "$3 $5" == '12.1 4008200e' ]];then
    [[ $after_reads == 1 && $fail != voltage-read-after ]] || return 1
    mem[$5]=$voltage
    [[ $fail != voltage-drift ]] || mem[$5]=0
    ((voltage_reads+=1));return 0
  fi
  local regs=(0e.1 0f.1 10.1 12.1 20.1 2a.1)
  local raw=(3 0 255 "$voltage" 193 13)
  [[ $reads -lt 6 && $3 == "${regs[$reads]}" && $5 == $(printf %x "$((values+reads))") ]] || return 1
  [[ $fail != read$reads ]] || return 1
  [[ $fail != bad-id || $reads != 0 ]] || raw[0]=0
  [[ $fail != bad-version || $reads != 1 ]] || raw[1]=1
  [[ $fail != dcdc-enable || $reads != 2 ]] || raw[2]=254
  mem[$5]=${raw[$reads]};((reads+=1));return 0
}
sunxi_axp() {
  [[ "$*" == 'pmu write 11 1f' && $ctrl == 23 && $before_reads == 1 && $reads == 6 && $voltage == 23 && $fdt == 1 && $commits == 0 && $writes == 0 ]] || return 1
  ((writes+=1))
  [[ $fail != power-write-before ]] || return 1
  ctrl=31
  [[ $fail != power-write-after ]] || return 1
  return 0
}
sleep() { [[ "$*" == 1 && $before_reads == 1 && $fail != settle ]] || return 1;settled=1; }
md.b() {
  case "$1 $2" in
    '4008200c 3') [[ $fail != diagnostic ]];;
    '40082010 6'|'40082012 2'|'4008200c 1'|'4008200d 1'|'4008200e 1') return 0;;
    *) return 1;;
  esac
}
booti() {
  [[ "$*" == '40080000 - 4fa00000' && $reads == 6 && $commits == 1 && $fdt == 1 && $ctrl == 31 && $settled == 1 && $after_reads == 1 && $voltage_reads == 1 && $voltage == 23 ]] || return 20
  [[ ${mem[4008400c]:-0} == $((16#434d4f31)) && $mode_loaded == 1 ]] || return 21
  [[ $initial_ctrl == 23 && $writes == 1 || $initial_ctrl == 31 && $writes == 0 ]] || return 22
  echo MOCK_BOOTI_REACHED;return 0
}
source "$test_source"
