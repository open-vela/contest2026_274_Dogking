#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Audited general-ASR A7TV export from v183 real-audio Linux evidence.

Private vendor-derived packages at captured VIP addresses, not a compiler,
generic relocator or RTOS hardware acceptance. Checks all bindings/outputs and
the existing single-task FE command profile before creating any A7TV files.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import tarfile

REPO = Path(__file__).resolve().parents[2]


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('work', type=Path, help='NEW proof/export directory')
    args = parser.parse_args()
    work = args.work.resolve()
    assert work.is_relative_to(REPO / 'out') and not work.exists()
    n = module('native', 'asr-native-v183.py')
    e = module('prepared', 'export-voice-prepared.py')
    kit = REPO / 'out/a733-linux-general-asr-v183-r1'
    contracts, _, _ = n.check(kit)
    golden = REPO / 'out/asr-native-v183-r1/unpacked/asr-real-r2'
    native_summary = json.loads(n.read(golden / 'summary.json'))
    assert native_summary['result'] == 'NATIVE_GENERAL_ASR_TEXT_MATCH'
    assert all(x['cpu_text_equal'] and x['cpu_tokens_equal'] and x['cpu_positions_equal']
               for x in native_summary['cases'])
    calls = {x['label']: x for x in map(json.loads, (golden / 'calls.jsonl').read_text().splitlines())}
    archive = REPO / 'out/asr-native-v183-r1/layout-qualified-r3-report.tar.gz'
    assert hashlib.sha256(archive.read_bytes()).hexdigest() == '1d40b0c2d437b253e4f64b67a59bc7c86e800e2e7e3a79bf6224f74f5101acbf'
    work.mkdir()
    with tarfile.open(archive) as t:
        total, count = 0, 0
        for item in t:
            path = Path(item.name)
            assert not path.is_absolute() and '..' not in path.parts and not item.issym() and not item.islnk()
            assert item.isdir() or item.isfile()
            assert item.size <= 64 * 1024**2
            total += item.size
            count += 1
            assert total <= 512 * 1024**2 and count <= 4096
            t.extract(item, work, filter='data')
    report = work / 'layout-qualified-r3'
    layout = json.loads(n.read(report / 'summary.json'))
    assert layout['result'] == 'GENERAL_ASR_NATIVE_LAYOUT_CAPTURED'
    assert {x['role'] for x in layout['models']} == set(contracts)
    assert all(x['native_exit'] == 0 and x['all_outputs_equal'] for x in layout['models'])
    base = work / 'fixture'
    base.mkdir()
    (base / 'models').mkdir()
    fixture = {'models': {}}
    for entry in layout['models']:
        role, label = entry['role'], entry['golden_label']
        assert role in contracts and label in calls and calls[label]['role'] == role
        model = base / 'models' / (role + '.nb')
        shutil.copyfile(kit / 'models' / model.name, model)
        folder = base / role
        folder.mkdir()
        inputs, outputs = [], []
        input_records, output_records = [], []
        for i, desc in enumerate(contracts[role]['input']):
            data = n.read(golden / label / 'input' / ('input%d.bin' % i), desc['bytes'])
            assert n.sha(data) == calls[label]['input_sha256'][i]
            path = folder / ('input%d.bin' % i)
            n.write(path, data)
            inputs.append(data)
            input_records.append(dict(path=str(path.relative_to(base)).replace('\\', '/'), sha256=n.sha(data)))
        for i, desc in enumerate(contracts[role]['output']):
            data = n.read(report / (role + '-output') / ('output%d.bin' % i), desc['bytes'])
            assert n.sha(data) == calls[label]['output_sha256'][i]
            outputs.append(data)
            output_records.append(dict(sha256=n.sha(data)))
        fixture['models'][role] = dict(model='models/' + model.name, inputs=input_records, outputs=output_records)
        trace = report / (role + '-trace')
        log = n.read(report / (role + '.log')).decode()
        records = [json.loads(line) for line in (trace / 'trace.jsonl').read_text().splitlines()]
        e.write_json(report / (role + '-trace-manifest.json'), e.L.trace_manifest(trace, 64 * 1024**2))
        e.write_json(report / (role + '-bindings.json'), e.L.bindings(log, trace, records, inputs, outputs, 64 * 1024**2))
    # Audit all models before writing the first package. The larger limits are
    # host evidence/export limits, not changes to the live firmware memory map.
    qualified = [e.model(base, report, contracts, fixture, role,
                          max_region=64 * 1024**2, max_pool=64 * 1024**2, max_package=64 * 1024**2,
                          allow_sequential_links=True)
                 for role in ('encoder', 'decoder', 'joiner')]
    destination = work / 'packages'
    destination.mkdir()
    index = []
    for image, metadata in qualified:
        role = metadata['model']
        path = destination / ('asr-' + role + '.a7tv')
        n.write(path, image)
        metadata.update(package=path.name, package_bytes=len(image), package_sha256=n.sha(image),
                        contract=contracts[role])
        index.append(metadata)
    result = dict(result='GENERAL_ASR_PREPARED_EXPORTED_NOT_RTOS_TESTED', schema=184,
                  cid='1000003b', source_layout_sha256=n.sha(n.read(report / 'summary.json')),
                  models=index, private_vendor_derived=True, generic_nbg_converter=False,
                  hardware_replay_passed=False, openvela_provider_ready=False, full_voice_chain=False)
    e.write_json(destination / 'export-index.json', result)
    for item in index:
        print(item['model'], item['package_bytes'], 'pool', item['pool_bytes'],
              'windows', item['mmu_windows'], item['package_sha256'], flush=True)


if __name__ == '__main__':
    main()
