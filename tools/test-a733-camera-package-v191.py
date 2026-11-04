#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Pure packaging-profile rejection tests; no image/SDK/hardware writes."""
from pathlib import Path
import runpy

repo = Path(__file__).resolve().parents[1]
check = runpy.run_path(str(repo/'tools/package-a733-boot-v188.py'))['configuration_profile']
common = ['CONFIG_A733_BOOT_DIAGNOSTICS=y', 'CONFIG_SMP_NCPUS=8',
          'CONFIG_BOARD_LATE_INITIALIZE=y', 'CONFIG_NSH_ARCHINIT=y',
          'CONFIG_BOARD_INITTHREAD_STACKSIZE=262144', 'CONFIG_BOARD_INITTHREAD_PRIORITY=100',
          'CONFIG_LIBC_MAX_EXITFUNS=0', 'CONFIG_A733_CPU_ACOUSTIC=y',
          'CONFIG_A733_NPU_GENERAL_ASR=y']
camera = common+['CONFIG_A733_IMX214_CHECKPOINT=y', 'CONFIG_SYSTEM_A733CAMERA=y']
control = camera+['CONFIG_A733_CAMERA_CONTROL_TEST=y']
count = 0


def expect_failure(config, purpose, base):
    global count
    try:
        check(config, purpose, base)
    except ValueError:
        count += 1
    else:
        raise AssertionError((config, purpose, base))


assert check(camera, 'camera-qualification', '190') == camera
for index in range(len(camera)):
    expect_failure(camera[:index]+camera[index+1:], 'camera-qualification', '190')
for base in ('185', '188', '189'):
    expect_failure(camera, 'camera-qualification', base)
for purpose, base in (('boot', '185'), ('tts-rebuffer', '188'), ('voice-pipeline', '189')):
    assert check(common, purpose, base) == common
    for extra in camera[-2:]:
        expect_failure(common+[extra], purpose, base)
expect_failure(camera, 'unknown', '190')
expect_failure(common, 'tts-rebuffer', '190')
expect_failure(common, 'voice-pipeline', '190')
assert check(control, 'camera-control', '191') == control
for index in range(len(control)):
    expect_failure(control[:index]+control[index+1:], 'camera-control', '191')
for base in ('185', '188', '189', '190'):
    expect_failure(control, 'camera-control', base)
for purpose, base in (('boot','185'),('tts-rebuffer','188'),('voice-pipeline','189'),
                       ('camera-qualification','190')):
    expect_failure(control if purpose=='camera-qualification' else common+control[-1:],purpose,base)
assert check(control, 'camera-id', '191') == control
for index in range(len(control)):
    expect_failure(control[:index]+control[index+1:], 'camera-id', '191')
for base in ('185','188','189','190','192'):
    expect_failure(control,'camera-id',base)
assert check(control,'camera-mode','191')==control
for index in range(len(control)):
    expect_failure(control[:index]+control[index+1:],'camera-mode','191')
for base in ('185','188','189','190','192','193'):
    expect_failure(control,'camera-mode',base)
assert check(control,'camera-packets','191')==control
for index in range(len(control)):
    expect_failure(control[:index]+control[index+1:],'camera-packets','191')
for base in ('185','188','189','190','192','193','194'):
    expect_failure(control,'camera-packets',base)
assert check(control,'camera-register-fix','191')==control
for index in range(len(control)):
    expect_failure(control[:index]+control[index+1:],'camera-register-fix','191')
for base in ('185','188','189','190','192','193','194','195'):
    expect_failure(control,'camera-register-fix',base)
assert check(control,'camera-frame','191')==control
for index in range(len(control)):
    expect_failure(control[:index]+control[index+1:],'camera-frame','191')
for base in ('185','188','189','190','192','193','194','195','196'):
    expect_failure(control,'camera-frame',base)
vision=control+['CONFIG_SYSTEM_A733NPU=y','CONFIG_SYSTEM_A733DISPLAY=y','CONFIG_MQ_MAXMSGSIZE=32']
assert check(vision,'camera-vision','191')==vision
for index in range(len(vision)):expect_failure(vision[:index]+vision[index+1:],'camera-vision','191')
for base in ('185','188','189','190','192','193','194','195','196','197'):expect_failure(vision,'camera-vision',base)
assert check(vision,'camera-sizing-fix','191')==vision
for index in range(len(vision)):expect_failure(vision[:index]+vision[index+1:],'camera-sizing-fix','191')
for base in ('185','188','189','190','192','193','194','195','196','197','198'):expect_failure(vision,'camera-sizing-fix',base)
print(f'PASS: {count} rejected package profiles; camera opt-in, boot/voice isolation, no files/devices written')
