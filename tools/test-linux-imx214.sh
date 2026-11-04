#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Qualification evidence only. Never flash, devmem, scan I2C, modprobe, change
# overlays/rails/clocks, install packages or touch UFS/SD block devices.
set -euo pipefail
umask 077
capture=0
video=/dev/video0
parent=.
while (($#)); do
  case "$1" in
    --capture) capture=1; shift ;;
    --video) (($#>=2)) || exit 2; video=$2; shift 2 ;;
    --output) (($#>=2)) || exit 2; parent=$2; shift 2 ;;
    --help)
      printf '%s\n' 'Usage: bash test-linux-imx214.sh [--capture] [--video /dev/video0] [--output NEW_REPORT_PARENT]' \
        'Default: inspect existing Linux device tree/media/clocks only. --capture explicitly requests 3 official 1080p NV12 frames.'
      exit 0 ;;
    *) printf 'Unknown option: %s\n' "$1" >&2; exit 2 ;;
  esac
done
[[ "$video" =~ ^/dev/video[0-9]+$ ]] || { echo 'Invalid video path' >&2; exit 2; }
for tool in timeout python3 tar mktemp; do
  command -v "$tool" >/dev/null || { echo "Missing required tool: $tool" >&2; exit 2; }
done
[[ -d "$parent" ]] || { echo 'Output parent must already exist' >&2; exit 2; }
report=$(mktemp -d "$parent/imx214-report-XXXXXXXX")
report=$(cd "$report" && pwd)
printf 'Evidence: %s\n' "$report"
run() {
  local name=$1 rc=0; shift
  timeout -k 2 15 "$@" >"$report/$name.log" 2>&1 || rc=$?
  printf '%s rc=%s\n' "$name" "$rc" >>"$report/commands.txt"
}
run kernel uname -a
run media-devices v4l2-ctl --list-devices
run selected-formats v4l2-ctl -d "$video" --list-formats-ext
run media-graph media-ctl -p
run source-plugin gst-inspect-1.0 v4l2src
run clock-summary cat /sys/kernel/debug/clk/clk_summary
run regulator-summary cat /sys/kernel/debug/regulator/regulator_summary
run kernel-camera-log bash -c 'dmesg | grep -Ei "imx214|sunxi.*(vin|csi)|mipi|sensor.*(detect|error)|isp.*(init|fail)|cci.*(error|fail)" | tail -n 250'
# Binary DT properties retain BE32/phandles without guessing from strings.
run device-tree python3 - "$report" <<'PY'
import json, pathlib, sys
root=pathlib.Path('/sys/firmware/devicetree/base')
dest=pathlib.Path(sys.argv[1]); records={}; count=0
wanted=('sensor','mipi','csi','camera','vind','twi3','twi4','i2c@2513','i2c@2514')
props={'model','compatible','status','reg','clock-frequency','clocks','clock-names',
 'pinctrl-0','pinctrl-1','pinctrl-names','pins','function','sensor0_mname',
 'sensor0_twi_addr','sensor0_twi_cci_id','sensor0_mclk_id','sensor0_reset',
 'sensor0_pwdn','sensor0_iovdd','sensor0_avdd','sensor0_dvdd','sensor0_afvdd',
 'sensor0_iovdd_vol','sensor0_avdd_vol','sensor0_dvdd_vol','sensor0_afvdd_vol',
 'data-lanes','clock-lanes','link-frequencies','csi_top','csi_isp'}
if root.is_dir():
    for node in root.rglob('*'):
        count+=1
        if count>65536: raise RuntimeError('Device-tree node bound')
        rel=node.relative_to(root).as_posix()
        if not node.is_file() or node.name not in props: continue
        if node.parent!=root and not any(p in rel.lower() for p in wanted): continue
        data=node.read_bytes()
        if len(data)>4096: raise RuntimeError('Property bound: '+rel)
        record={'hex':data.hex()}
        if data and all(32<=b<127 or b==0 for b in data):
            record['strings']=data.rstrip(b'\0').decode('ascii').split('\0')
        if data and len(data)%4==0:
            record['be32']=[int.from_bytes(data[i:i+4],'big') for i in range(0,len(data),4)]
        records[rel]=record
(dest/'device-tree.json').write_text(json.dumps(records,indent=2))
print('properties',len(records),'nodes_seen',count)
PY
if ((capture)); then
  if [[ ! -c "$video" ]]; then
    echo 'CAPTURE FAILED: selected node missing/non-character' >"$report/capture-status.txt"
  else
    # This invokes the existing official Linux ISP, NOT an RTOS driver.
    # No fallback to a different camera or invented raw/YUV interpretation.
    rc=0
    timeout -k 2 20 gst-launch-1.0 -e v4l2src device="$video" en-awisp=1 en-largemode=0 \
      num-buffers=3 '!' 'video/x-raw,format=NV12,width=1920,height=1080,framerate=30/1' \
      '!' filesink location="$report/frames-1920x1080.nv12.partial" \
      >"$report/capture.log" 2>&1 || rc=$?
    python3 - "$report" "$rc" <<'PY'
import hashlib,json,pathlib,sys
p=pathlib.Path(sys.argv[1]); rc=int(sys.argv[2]); f=p/'frames-1920x1080.nv12.partial'
size=f.stat().st_size if f.exists() else 0
good=rc==0 and size==3*1920*1080*3//2
result={'official_Linux_capture':good,'exit':rc,'bytes':size,'frames':3,'width':1920,
 'height':1080,'format':'NV12','openvela_capture':False}
if good:
    target=f.with_suffix('');f.rename(target)
    result['sha256']=hashlib.sha256(target.read_bytes()).hexdigest()
(p/'capture-status.json').write_text(json.dumps(result,indent=2))
print('Linux capture', 'PASS' if good else 'FAILED; partial preserved, do not use as golden')
PY
  fi
else
  echo 'Capture NOT requested; no stream/power/mode changes made by this script' >"$report/capture-status.txt"
fi
# Record existing bound sensor driver and camera clocks after optional capture.
run post-clock-summary cat /sys/kernel/debug/clk/clk_summary
run sensor-bindings python3 - <<'PY'
import pathlib
for p in sorted(pathlib.Path('/sys/bus/i2c/devices').glob('*')):
    n=p/'name'
    if n.is_file():
        name=n.read_text().strip()
        if any(x in name.lower() for x in ('imx','camera','ov13850')):
            print(p.name,repr(name),'driver=',(p/'driver').resolve())
PY
python3 - "$report" <<'PY'
import hashlib,json,pathlib,sys
p=pathlib.Path(sys.argv[1]);inventory={}
for f in p.iterdir():
    if f.is_file():
        h=hashlib.sha256()
        with f.open('rb') as s:
            while b:=s.read(1048576):h.update(b)
        inventory[f.name]={'bytes':f.stat().st_size,'sha256':h.hexdigest()}
(p/'sha256.json').write_text(json.dumps(inventory,indent=2))
PY
tar -czf "$report.tar.gz" -C "$(dirname "$report")" "$(basename "$report")"
printf '\nREPORT: %s.tar.gz\n' "$report"
printf '%s\n' 'No flashing/install/overlay or direct register writes; optional capture uses the existing Linux driver. NOT openvela capture acceptance.'
