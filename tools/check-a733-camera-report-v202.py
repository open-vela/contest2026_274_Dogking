#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""NEW offline evidence directory; runs v201 base checks and v202 tests."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('report202_check', ROOT / 'tools/a733-camera-report-v202.py')
r = importlib.util.module_from_spec(spec)
spec.loader.exec_module(r)


def demo(output):
    """UI-only synthetic chart, no COMPLETE/raw/inference pretence."""
    model = bytearray([114]) * (640 * 640 * 3)
    preview = bytearray(128 * 160 * 3)
    for y in range(360):
        for x in range(640):
            color = [32, 32, 32]
            color[min(2, x * 3 // 640)] = 128 + y * 127 // 359
            at = ((y + 140) * 640 + x) * 3
            model[at:at + 3] = bytes(color)
    for y in range(72):
        for x in range(128):
            at = (((5 * y + 2) + 140) * 640 + 5 * x + 2) * 3
            preview[((y + 44) * 128 + x) * 3:((y + 44) * 128 + x) * 3 + 3] = model[at:at + 3]
    images, rows = r.visualizations(model, preview, None)
    report = dict(result='SYNTHETIC_UI_DEMO_NOT_CAMERA_OR_NPU', synthetic_demo=True,
                  raw=dict(min=0, max=1023, mean=512, standard_deviation=0,
                           zero_fraction=0, saturation_fraction=0,
                           warnings=['SYNTHETIC_DEMO: example numbers, not measured RAW statistics']),
                  input_crc32='NOT-MEASURED', bayer='NOT-QUALIFIED', inference_bound=False,
                  detections=rows, hardware_verified=False)
    output.mkdir(exist_ok=False)
    files = {**images, 'report.html': r.document(report, images, 'HOST SYNTHETIC / NOT BOARD EVIDENCE').encode('utf-8'),
             'DEMO.json': json.dumps(report, indent=2).encode()}
    for name, data in files.items():
        with (output / name).open('xb') as stream:
            stream.write(data)
    return {name: r.sha(data) for name, data in files.items()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--verify-image', action='store_true')
    args = parser.parse_args()
    output = args.output.resolve()
    r.v.require(not output.exists() and (output.is_relative_to(ROOT / 'out') or
                output.is_relative_to(ROOT / 'docs/evidence')), 'NEW output under out or docs/evidence required')
    output.mkdir(parents=True, exist_ok=False)
    base = [sys.executable, str(ROOT / 'tools/check-a733-camera-vision-v201.py'),
            '--output', str(output / 'v201-base')]
    if args.verify_image:
        base.append('--verify-image')
    tests = [base, [sys.executable, str(ROOT / 'tools/test-a733-camera-report-v202.py')]]
    records = []
    for index, command in enumerate(tests):
        run = subprocess.run(command, cwd=ROOT, capture_output=True, text=True,
                             encoding='utf-8', errors='replace', timeout=240,
                             env={**os.environ, 'PYTHONUTF8': '1'})
        log = f'check-{index}.log'
        data = (run.stdout + run.stderr).encode('utf-8')
        with (output / log).open('xb') as stream:
            stream.write(data)
        records.append(dict(command=command, exit=run.returncode, log=log, sha256=r.sha(data)))
        r.v.require(run.returncode == 0, 'Regression failed; inspect ' + str(output / log))
        print('PASS:', command[1], flush=True)
    source_head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT).decode().strip()
    inputs = ['tools/a733-camera-report-v202.py', 'tools/test-a733-camera-report-v202.py',
              'tools/check-a733-camera-report-v202.py', 'tools/a733-camera-vision-v201.py']
    report = dict(result='HOST_REPORT_CHECKS_PASSED_NOT_BOARD_TESTED', version=202,
                  source_head=source_head, source_sha256={name: r.sha((ROOT / name).read_bytes()) for name in inputs},
                  tests=records, demo_sha256=demo(output / 'synthetic-ui-demo'),
                  v201_base_summary_sha256=r.sha((output / 'v201-base/summary.json').read_bytes()),
                  firmware_modified=False, new_image=False, board_connected=False,
                  hardware_verified=False, cpu_oracle_all_bayer_run=os.name == 'posix')
    with (output / 'summary.json').open('x', encoding='utf-8', newline='\n') as stream:
        json.dump(report, stream, indent=2)
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
