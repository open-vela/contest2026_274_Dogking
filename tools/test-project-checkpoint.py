#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
import hashlib
import importlib.util
import json
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location('checkpoint', Path(__file__).with_name('checkpoint-a733-project.py'))
checkpoint = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checkpoint)


class CheckpointTests(unittest.TestCase):
    def make_repo(self, root):
        subprocess.run(['git', 'init', '-q', str(root)], check=True)
        subprocess.run(['git', '-C', str(root), 'config', 'core.autocrlf', 'false'], check=True)
        (root/'tracked.c').write_bytes(b'old\n')
        (root/'deleted.c').write_bytes(b'delete me\n')
        (root/'.gitignore').write_text('out/\n.env\n', encoding='utf-8')
        subprocess.run(['git', '-C', str(root), 'add', '.'], check=True)
        subprocess.run(['git', '-C', str(root), '-c', 'user.name=CheckpointTest',
            '-c', 'user.email=test@example.invalid', 'commit', '-qm', 'fixture'], check=True)
        (root/'tracked.c').write_bytes(b'changed\n')
        (root/'deleted.c').unlink()
        (root/'untracked.c').write_bytes(b'new\n')
        (root/'staged.c').write_bytes(b'index version\n')
        subprocess.run(['git', '-C', str(root), 'add', 'staged.c'], check=True)
        (root/'staged.c').write_bytes(b'working version\n')
        (root/'.env').write_bytes(b'ignored secret\n')
        (root/'docs').mkdir()
        (root/'docs/history.md').write_text('历史不删\n', encoding='utf-8')
        return root

    def test_roundtrip(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = self.make_repo(Path(tmp)/'repo')
            reference = Path(tmp)/'image.img'
            reference.write_bytes(b'fake image, no HW')
            before = checkpoint.git(root, 'status', '--porcelain=v1', '-uall')
            result = checkpoint.create(root, 'baseline', [reference])
            saved = Path(result['path'])
            manifest = checkpoint.verify(saved, True)
            self.assertIn('untracked.c', manifest['files'])
            self.assertIn('docs/history.md', manifest['files'])
            self.assertNotIn('.env', manifest['files'])
            self.assertEqual(manifest['missing_tracked_paths'], ['deleted.c'])
            self.assertEqual(checkpoint.git(root, 'status', '--porcelain=v1', '-uall'), before)
            with self.assertRaises(ValueError): checkpoint.create(root, 'baseline', [])
            restored = Path(tmp)/'restored'
            checkpoint.restore(saved, restored)
            self.assertEqual((restored/'tracked.c').read_bytes(), b'changed\n')
            self.assertEqual((restored/'untracked.c').read_bytes(), b'new\n')
            self.assertFalse((restored/'deleted.c').exists())
            self.assertFalse((restored/'.git').exists())
            git_restored = Path(tmp)/'restored-git'
            checkpoint.restore(saved, git_restored, with_git=True)
            self.assertEqual(checkpoint.git(git_restored, 'rev-parse', 'HEAD').decode().strip(), manifest['head'])
            self.assertEqual(checkpoint.git(git_restored, 'status', '--porcelain=v1', '-uall'), before)
            self.assertEqual((git_restored/'tracked.c').read_bytes(), b'changed\n')
            self.assertEqual(checkpoint.git(git_restored, 'show', ':staged.c'), b'index version\n')
            self.assertEqual((git_restored/'staged.c').read_bytes(), b'working version\n')
            with self.assertRaises(ValueError): checkpoint.restore(saved, restored)
            self.assertEqual((root/'tracked.c').read_bytes(), b'changed\n')
            reference.write_bytes(b'changed reference')
            checkpoint.verify(saved)  # references are optional, no fake pass
            with self.assertRaises(ValueError): checkpoint.verify(saved, True)
            (saved/'working-tree.patch').write_bytes(b'corrupted')
            with self.assertRaises(ValueError): checkpoint.verify(saved)

    def test_corruption_and_paths(self):
        for name in ['../x', '/x', 'a/../b', 'a//b', 'a\\b', 'C:/x', './x', '.git/config']:
            with self.assertRaises(ValueError): checkpoint.safe_name(name)
        with tempfile.TemporaryDirectory() as tmp:
            root = self.make_repo(Path(tmp)/'repo')
            with self.assertRaises(ValueError): checkpoint.create(root, '../escape', [])
            saved = Path(checkpoint.create(root, 'corrupt-test', [])['path'])
            complete = (saved/'COMPLETE').read_bytes()
            (saved/'COMPLETE').write_bytes(b'not complete\n')
            with self.assertRaises(ValueError): checkpoint.verify(saved)
            (saved/'COMPLETE').write_bytes(complete)
            archive = saved/'project.tar.gz'
            archive.write_bytes(archive.read_bytes()+b'corrupt')
            with self.assertRaises(ValueError): checkpoint.verify(saved)
            with self.assertRaises(ValueError): checkpoint.restore(saved, Path(tmp)/'no-restore')
            self.assertFalse((Path(tmp)/'no-restore').exists())

    def test_deep_relative_destination(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = self.make_repo(Path(tmp)/'repo')
            saved = Path(checkpoint.create(root, 'deep-path', [])['path'])
            previous = Path.cwd()
            try:
                os.chdir(tmp)
                relative = Path('a'*80)/('b'*80)/('c'*80)/'restored'
                checkpoint.restore(saved, relative)
                self.assertEqual(checkpoint.io_path(relative/'tracked.c').read_bytes(), b'changed\n')
            finally:
                os.chdir(previous)
                owned = (Path(tmp)/('a'*80)).resolve()
                self.assertTrue(owned.is_relative_to(Path(tmp).resolve()))
                if checkpoint.io_path(owned).exists():
                    shutil.rmtree(checkpoint.io_path(owned))


if __name__ == '__main__': unittest.main()
