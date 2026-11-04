#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Prepare, but never publish, the frozen v229 Git snapshot and upload manifest."""
import argparse
import fnmatch
import hashlib
import json
import re
import subprocess
import zipfile
from pathlib import Path, PurePosixPath

ROOT = Path(__file__).resolve().parents[1]
FIRMWARE = 'ba0d722a965ed59a8aa8bef49dbf64fc1f8731e5'


def git(*args):
    return subprocess.check_output(['git', *args], cwd=ROOT)


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(4 * 1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    destination = args.output.resolve()
    if destination.exists():
        raise SystemExit('STOP: output already exists; select a new revision')
    commit = git('rev-parse', 'HEAD').decode().strip()
    pointer = json.loads(git('show', 'HEAD:docs/FINAL_RELEASE.json'))
    if pointer['version'] != 'v229' or pointer['dslocal_included']:
        raise SystemExit('STOP: wrong final release pointer')
    if git('diff', '--name-only', FIRMWARE, 'HEAD', '--', 'board', 'app', 'patches').strip():
        raise SystemExit('STOP: committed firmware differs from frozen v229')
    if git('diff', '--name-only', 'HEAD', '--', 'board', 'app', 'patches').strip():
        raise SystemExit('STOP: uncommitted firmware changes exist')
    if git('diff', '--cached', '--name-only').strip():
        raise SystemExit('STOP: staged changes are not part of HEAD')
    image = ROOT.parent / pointer['image']
    kernel = ROOT / 'out/v229-product-v30prefix2-release-r1/Image'
    if image.stat().st_size != pointer['image_bytes']:
        raise SystemExit('STOP: image size mismatch')
    image_hash, kernel_hash = digest(image), digest(kernel)
    if image_hash != pointer['image_sha256'] or kernel_hash != pointer['kernel_sha256']:
        raise SystemExit('STOP: image/kernel SHA256 mismatch')

    expected = {}
    for entry in git('ls-tree', '-r', '-z', 'HEAD').split(b'\0'):
        if not entry:
            continue
        metadata, name = entry.split(b'\t', 1)
        mode, kind, oid = metadata.decode().split()
        if kind != 'blob' or mode not in ('100644', '100755'):
            raise SystemExit('STOP: non-regular Git entry requires manual review')
        expected[name.decode('utf-8')] = oid
    if any('dslocal' in p.lower() or re.search(r'(^|[-_/])v230([._/-]|$)', p.lower())
           for p in expected):
        raise SystemExit('STOP: abandoned v230 draft is tracked')
    destination.mkdir(parents=True)
    archive = destination / 'Vela-openvela-v229-source.zip'
    # Windows Git otherwise applies core.autocrlf to archive members. Export the
    # exact Git blob bytes, including byte-sealed evidence, on every host.
    git('-c', 'core.autocrlf=false', '-c', 'core.eol=lf',
        'archive', '--format=zip', '--prefix=Vela-openvela-v229/',
        '--output=' + str(archive), 'HEAD')

    forbidden = ('*.img', '*.gguf', '*.a7tv', '*.a7pm', '*.nb', '*.key',
                 '*.pem', '*.p12', '*.credentials', '*.password', '.env')
    patterns = {
        'private-key': re.compile(rb'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----'),
        'provider-token': re.compile(rb'\bsk-[A-Za-z0-9_-]{20,}\b'),
        'cloud-access-key': re.compile(rb'\b(?:AKID[A-Za-z0-9]{20,}|LTAI[A-Za-z0-9]{16,})\b'),
    }
    files, findings = [], []
    with zipfile.ZipFile(archive) as source:
        if source.testzip() is not None:
            raise SystemExit('STOP: source ZIP CRC failed')
        seen = set()
        for item in source.infolist():
            if item.is_dir():
                continue
            prefix = 'Vela-openvela-v229/'
            if not item.filename.startswith(prefix):
                raise SystemExit('STOP: invalid archive prefix')
            name = item.filename[len(prefix):]
            if '..' in PurePosixPath(name).parts or name not in expected or name in seen:
                raise SystemExit('STOP: unsafe, extra or duplicate archive member')
            seen.add(name)
            if item.file_size >= 100 * 1024 * 1024:
                findings.append({'path': name, 'type': 'large-file'})
            if any(fnmatch.fnmatch(PurePosixPath(name).name.lower(), p) for p in forbidden):
                findings.append({'path': name, 'type': 'private-or-model-binary'})
            data = source.read(item)
            oid = hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest()
            if oid != expected[name]:
                raise SystemExit('STOP: archive bytes do not match Git object: ' + name)
            for kind, pattern in patterns.items():
                if pattern.search(data):
                    findings.append({'path': name, 'type': kind})  # Never print the value.
            files.append({'path': name, 'bytes': len(data), 'git_blob': oid,
                          'sha256': hashlib.sha256(data).hexdigest()})
        if seen != set(expected):
            raise SystemExit('STOP: Git files missing from archive')
    manifest = {
        'version': 'v229', 'date': '2026-11-04', 'source_commit': commit,
        'firmware_commit': FIRMWARE, 'firmware_diff_empty': True,
        'source_archive': archive.name, 'source_sha256': digest(archive),
        'source_bytes': archive.stat().st_size, 'source_files': len(files),
        'source_uncompressed_bytes': sum(f['bytes'] for f in files),
        'image': image.name, 'image_bytes': image.stat().st_size, 'image_sha256': image_hash,
        'kernel_sha256': kernel_hash, 'dslocal_included': False,
        'heuristic_credential_findings': findings,
        'credential_scan_is_complete_privacy_audit': False,
        'image_credential_scan': 'not repeated here; use original sealed image, not board SD dump',
        'v229_cloud_board_tested': False, 'git_pushed': False, 'PR_created': False,
        'files': files,
    }
    (destination / 'MANIFEST.json').write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    (destination / 'SHA256SUMS.txt').write_text(
        manifest['source_sha256'] + '  ' + archive.name + '\n' +
        image_hash + '  ' + image.name + '\n', encoding='utf-8')
    (destination / 'UPLOAD_README.md').write_text(
        '# v229 上传准备包\n\n源码提交：`' + commit + '`。\n\n'
        '源码ZIP可作为最终源码快照；不含Git历史、SDK、模型、板上凭据或未跟踪材料。'
        '已逐条回读ZIP并与Git对象匹配，固件保持v229冻结实现。\n\n'
        '2GiB镜像未复制，仍在此目录上一级：`' + image.name + '`。'
        '按SHA256核验后另行上传Release附件/网盘，不加入源码Git。\n\n'
        '不要上传archives、out、私人录音或板上/data配置；答辩三张图片另见工作区决赛答辩目录。'
        '自动扫描不是完整隐私审计，公开前仍须人工检查原始日志和第三方授权。\n\n'
        'v229最终提交不等于新版云多轮实机验收；历史稳定回退v219仍保留。'
        '本工具不推送、不创建PR，不写SD/UFS。\n', encoding='utf-8')
    if findings:
        raise SystemExit('STOP: upload findings require manual review; see redacted MANIFEST.json')
    print(json.dumps({k: v for k, v in manifest.items() if k != 'files'},
                     ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
