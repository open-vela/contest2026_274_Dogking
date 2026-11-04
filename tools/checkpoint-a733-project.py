#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Immutable local development checkpoint. No original Git mutation/network/HW.

Captures Git-listed current files (including untracked/unignored sources),
tracked deletions, dirty patch, reachable Git history, and external-file hashes.
Restore ONLY to a new directory; never roll back an existing working tree.
"""
import argparse
import hashlib
import io
import json
import os
import re
import subprocess
import tarfile
from datetime import datetime, timezone
from pathlib import Path, PurePosixPath

MAX_FILES = 10000
MAX_FILE = 64 * 1024 * 1024
MAX_TOTAL = 512 * 1024 * 1024


def io_path(path):
    """Extended absolute Windows path, independent of system MAX_PATH policy."""
    path = path.absolute()
    text = str(path)
    if os.name == 'nt' and not text.startswith('\\\\?\\'):
        text = '\\\\?\\UNC\\' + text[2:] if text.startswith('\\\\') else '\\\\?\\' + text
        return Path(text)
    return path


def sha_file(path):
    digest = hashlib.sha256()
    with io_path(path).open('rb') as stream:
        while block := stream.read(1024 * 1024):
            digest.update(block)
    return digest.hexdigest()


def safe_name(name):
    if (not isinstance(name, str) or not name or '\\' in name or ':' in name or
            '\x00' in name or name.startswith('/') or
            any(x in ('', '.', '..', '.git') for x in name.split('/'))):
        raise ValueError('Unsafe archive path')
    return PurePosixPath(name)


def git(repo, *args):
    return subprocess.check_output(['git', '--no-optional-locks', '-c', 'core.longpaths=true', '-c', 'core.autocrlf=false', '-c',
        'core.quotepath=false', '-C', str(repo), *args], stderr=subprocess.PIPE)


def dump_new(path, value):
    with io_path(path).open('x', encoding='utf-8', newline='\n') as stream:
        json.dump(value, stream, indent=2, ensure_ascii=True)
        stream.write('\n')


def create(repo, label, references):
    repo = repo.resolve(strict=True)
    if not re.fullmatch(r'[a-z0-9][a-z0-9-]{0,63}', label):
        raise ValueError('Use a short lowercase letters/digits/hyphens label')
    if Path(os.fsdecode(git(repo, 'rev-parse', '--show-toplevel')).strip()).resolve() != repo:
        raise ValueError('Repository root required')
    destination = repo / 'out/checkpoints' / label
    if not destination.resolve().is_relative_to(repo):
        raise ValueError('Checkpoint parent escapes repository')
    if io_path(destination).exists() or io_path(destination).is_symlink():
        raise ValueError('Checkpoint exists; use a NEW label')
    tracked = {os.fsdecode(p) for p in git(repo, 'ls-tree', '-r', '--name-only', '-z', 'HEAD').split(b'\0') if p}
    indexed = {os.fsdecode(p) for p in git(repo, 'ls-files', '--cached', '-z').split(b'\0') if p}
    names = sorted(tracked | indexed | {os.fsdecode(p) for p in git(repo, 'ls-files',
        '--others', '--exclude-standard', '-z').split(b'\0') if p})
    if not names or len(names) > MAX_FILES:
        raise ValueError('File count outside checkpoint bound')
    source_files, missing, total = [], [], 0
    for name in names:
        relative = safe_name(name)
        source = repo.joinpath(*relative.parts)
        if io_path(source).is_symlink() or not source.resolve().is_relative_to(repo):
            raise ValueError('Symlink/escaping input: ' + name)
        if not io_path(source).exists():
            if name not in tracked | indexed:
                raise ValueError('Untracked input disappeared: ' + name)
            missing.append(name)  # tracked deletion; never recreate from HEAD
            continue
        if not io_path(source).is_file():
            raise ValueError('Non-regular input: ' + name)
        size = io_path(source).stat().st_size
        if size > MAX_FILE or total + size > MAX_TOTAL:
            raise ValueError('Source size bound; put large artifacts in explicit references')
        source_files.append((name, io_path(source)))
        total += size
    refs = []
    for raw in references:
        path = raw.resolve(strict=True)
        if not io_path(path).is_file():
            raise ValueError('Reference must be a regular file')
        refs.append(dict(path=str(path), bytes=io_path(path).stat().st_size, sha256=sha_file(path)))
    metadata = dict(head=git(repo, 'rev-parse', 'HEAD').decode().strip(),
        refs=git(repo, 'for-each-ref', '--format=%(refname) %(objectname)').decode(),
        original_repo=str(repo), utc=datetime.now(timezone.utc).isoformat(),
        missing_tracked_paths=missing, references=refs)
    io_path(destination).mkdir(parents=True, exist_ok=False)
    # Keep any interrupted checkpoint for diagnosis. No COMPLETE => not usable.
    io_path(destination/'git-status.txt').write_bytes(git(repo, 'status', '--porcelain=v1', '-uall'))
    io_path(destination/'working-tree.patch').write_bytes(git(repo, 'diff', '--binary', '--no-ext-diff', 'HEAD'))
    io_path(destination/'index.patch').write_bytes(git(repo, 'diff', '--cached', '--binary', '--no-ext-diff', 'HEAD'))
    git(repo, 'bundle', 'create', str(destination/'git-history.bundle'), '--all', 'HEAD')
    records = {}
    archive_path = destination/'project.tar.gz'
    with tarfile.open(io_path(archive_path), 'x:gz') as archive:
        for name, source in source_files:
            before = source.stat()
            with source.open('rb') as stream:
                data = stream.read(MAX_FILE + 1)
            after = source.stat()
            if len(data) != before.st_size or (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns):
                raise ValueError('Source changed during checkpoint: ' + name)
            info = tarfile.TarInfo(name)
            info.size = len(data)
            info.mode = before.st_mode & 0o777
            info.mtime = int(before.st_mtime)
            archive.addfile(info, io.BytesIO(data))
            records[name] = dict(bytes=len(data), sha256=hashlib.sha256(data).hexdigest(), mode=info.mode)
    metadata.update(schema=1, label=label, files=records, source_bytes=total,
        archive=dict(name='project.tar.gz', bytes=io_path(archive_path).stat().st_size, sha256=sha_file(archive_path)),
        auxiliary={name: sha_file(destination/name) for name in ['git-status.txt', 'working-tree.patch', 'index.patch', 'git-history.bundle']},
        hardware_verified=False,
        note='Local integrity checkpoint, NOT a boot/HW acceptance claim. Ignored SD images/models/credentials are not copied; references are hashes only. Contains project logs/history; keep private.')
    dump_new(destination/'manifest.json', metadata)
    with io_path(destination/'COMPLETE').open('x', encoding='ascii', newline='\n') as stream:
        stream.write(sha_file(destination/'manifest.json') + '\n')
    verify(destination, check_references=True)
    return dict(result='CHECKPOINT_CREATED', path=str(destination), files=len(records),
        deleted=len(missing), source_bytes=total, references=len(refs), hardware_verified=False)


def verify(checkpoint, check_references=False):
    checkpoint = checkpoint.resolve(strict=True)
    checkpoint = io_path(checkpoint)
    manifest_path = checkpoint/'manifest.json'
    if manifest_path.stat().st_size > 8 * 1024 * 1024:
        raise ValueError('Manifest too large')
    if (checkpoint/'COMPLETE').read_bytes() != (sha_file(manifest_path)+'\n').encode('ascii'):
        raise ValueError('Checkpoint is incomplete or manifest changed')
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    files = manifest['files']
    if manifest['schema'] != 1 or not 0 < len(files) <= MAX_FILES:
        raise ValueError('Unsupported manifest/count')
    total = 0
    for name, record in files.items():
        safe_name(name)
        if not 0 <= record['bytes'] <= MAX_FILE:
            raise ValueError('Invalid file length')
        total += record['bytes']
    if total > MAX_TOTAL or total != manifest['source_bytes']:
        raise ValueError('Invalid total size')
    if manifest['archive']['name'] != 'project.tar.gz':
        raise ValueError('Unsupported archive name')
    path = checkpoint/'project.tar.gz'
    if path.stat().st_size != manifest['archive']['bytes'] or sha_file(path) != manifest['archive']['sha256']:
        raise ValueError('Archive hash/size mismatch')
    if set(manifest['auxiliary']) != {'git-status.txt', 'working-tree.patch', 'index.patch', 'git-history.bundle'}:
        raise ValueError('Unexpected auxiliary paths')
    for name, digest in manifest['auxiliary'].items():
        if sha_file(checkpoint/name) != digest:
            raise ValueError('Auxiliary file hash mismatch')
    seen = set()
    with tarfile.open(path, 'r:gz') as archive:
        for member in archive:
            safe_name(member.name)
            if len(seen) >= MAX_FILES or member.name in seen or member.name not in files or not member.isfile():
                raise ValueError('Unexpected/duplicate/non-regular member')
            record = files[member.name]
            if member.size != record['bytes']:
                raise ValueError('Member length mismatch')
            data = archive.extractfile(member).read()
            if hashlib.sha256(data).hexdigest() != record['sha256']:
                raise ValueError('Member hash mismatch: '+member.name)
            seen.add(member.name)
    if seen != set(files):
        raise ValueError('Missing archive members')
    if check_references:
        for reference in manifest['references']:
            path = Path(reference['path'])
            if io_path(path).stat().st_size != reference['bytes'] or sha_file(path) != reference['sha256']:
                raise ValueError('External reference missing/changed: '+str(path))
    return manifest


def restore(checkpoint, destination, with_git=False):
    # Relative Windows paths retain MAX_PATH limits even in long-path-aware
    # processes. Absolute paths also keep post-clone I/O independent of cwd.
    checkpoint = checkpoint.resolve(strict=True)
    destination = destination.resolve()
    manifest = verify(checkpoint)
    if io_path(destination).exists() or io_path(destination).is_symlink():
        raise ValueError('Restore requires a NEW directory; existing files are never overwritten')
    if not re.fullmatch(r'[0-9a-f]{40}|[0-9a-f]{64}', manifest['head']):
        raise ValueError('Invalid saved Git HEAD')
    if with_git:
        bundle = (checkpoint/'git-history.bundle').resolve(strict=True)
        subprocess.check_output(['git', '--no-optional-locks', '-c', 'core.longpaths=true', 'clone', '--no-checkout',
            '--origin', 'recovery', str(bundle), str(destination)], stderr=subprocess.PIPE)
        # Only this NEW repository is modified. No checkout/overwrite of files.
        git(destination, 'update-ref', '--no-deref', 'HEAD', manifest['head'])
        git(destination, 'read-tree', manifest['head'])
        index_patch = (checkpoint/'index.patch').resolve(strict=True)
        if io_path(index_patch).stat().st_size:
            git(destination, 'apply', '--cached', '--binary', str(index_patch))
    else:
        io_path(destination).mkdir(parents=True, exist_ok=False)
    # No extractall, links, original Git reset/checkout, image writes/services.
    with tarfile.open(io_path(checkpoint/'project.tar.gz'), 'r:gz') as archive:
        for member in archive:
            relative = safe_name(member.name)
            if member.name not in manifest['files'] or not member.isfile():
                raise ValueError('Archive changed after verification')
            record = manifest['files'][member.name]
            if member.size != record['bytes']:
                raise ValueError('Archive length changed after verification')
            data = archive.extractfile(member).read()
            if hashlib.sha256(data).hexdigest() != record['sha256']:
                raise ValueError('Archive content changed after verification')
            target = io_path(destination.joinpath(*relative.parts))
            target.parent.mkdir(parents=True, exist_ok=True)
            with target.open('xb') as stream:
                stream.write(data)
            os.chmod(target, manifest['files'][member.name]['mode'])
    for name, record in manifest['files'].items():
        if sha_file(destination.joinpath(*safe_name(name).parts)) != record['sha256']:
            raise ValueError('Restored content mismatch')
    return dict(result='RESTORED_TO_NEW_DIRECTORY', path=str(destination.resolve()),
        files=len(manifest['files']), git_history_included=with_git, hardware_verified=False,
        note='Original repository untouched. Optional Git restore uses a detached saved HEAD in a NEW repository. This is not a flashed system.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    create_p = commands.add_parser('create')
    create_p.add_argument('label')
    create_p.add_argument('--repo', type=Path, default=Path(__file__).resolve().parent.parent)
    create_p.add_argument('--reference', type=Path, action='append', default=[])
    verify_p = commands.add_parser('verify')
    verify_p.add_argument('checkpoint', type=Path)
    verify_p.add_argument('--check-references', action='store_true')
    restore_p = commands.add_parser('restore')
    restore_p.add_argument('checkpoint', type=Path)
    restore_p.add_argument('destination', type=Path)
    restore_p.add_argument('--with-git', action='store_true', help='Initialize NEW repo from bundled history; restore dirty files without checkout')
    args = parser.parse_args()
    try:
        if args.command == 'create':
            result = create(args.repo, args.label, args.reference)
        elif args.command == 'restore':
            result = restore(args.checkpoint, args.destination, args.with_git)
        else:
            manifest = verify(args.checkpoint, args.check_references)
            result = dict(result='CHECKPOINT_INTEGRITY_MATCHED', files=len(manifest['files']),
                references_checked=args.check_references, hardware_verified=False)
        print(json.dumps(result, ensure_ascii=True, indent=2))
    except (OSError, ValueError, KeyError, TypeError, tarfile.TarError, subprocess.CalledProcessError) as error:
        parser.exit(1, 'STOP: '+str(error)+'\n')
