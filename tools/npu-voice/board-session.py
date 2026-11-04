#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Operator-driven SSH test session. Password is prompted, never stored.

JSON-line operations: run(command,timeout), put(local,remote), get(remote,local),
mkdir(remote), quit. Uses an already trusted known_hosts file; no auto-trust,
remote reset, retry, flashing, installation or background jobs of its own.
Install paramiko into a private host directory, supplied with --deps.
"""
import argparse
import getpass
import json
from pathlib import Path
import sys
import time


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host', required=True)
    p.add_argument('--user', default='radxa')
    p.add_argument('--known-hosts', type=Path, required=True)
    p.add_argument('--deps', type=Path, required=True)
    a = p.parse_args()
    sys.path.insert(0, str(a.deps.resolve()))
    import paramiko
    client = paramiko.SSHClient()
    client.load_host_keys(str(a.known_hosts.resolve()))
    client.set_missing_host_key_policy(paramiko.RejectPolicy())
    password = getpass.getpass('Board SSH password (not saved): ')
    client.connect(a.host, username=a.user, password=password, timeout=8,
                   banner_timeout=8, auth_timeout=8, look_for_keys=False,
                   allow_agent=False)
    del password
    client.get_transport().set_keepalive(15)
    sftp = client.open_sftp()
    print('BOARD_SESSION_READY', flush=True)
    try:
        for line in sys.stdin:
            try:
                v = json.loads(line)
                op = v['op']
                if op == 'quit':
                    break
                if op == 'run':
                    timeout = min(60, max(1, int(v.get('timeout', 30))))
                    channel = client.get_transport().open_session(timeout=8)
                    channel.set_combine_stderr(True)
                    channel.exec_command(v['command'])
                    if v.get('sudo_password'):
                        secret = getpass.getpass('sudo password (not saved): ')
                        channel.sendall((secret + '\n').encode())
                        del secret
                    until = time.monotonic() + timeout
                    out = bytearray()
                    while True:
                        while channel.recv_ready():
                            out.extend(channel.recv(32768))
                            if len(out) > 4 * 1024 * 1024:
                                raise ValueError('bounded SSH output exceeded')
                        if channel.exit_status_ready() and not channel.recv_ready():
                            break
                        if time.monotonic() > until:
                            channel.close()
                            raise TimeoutError('SSH wait expired; remote state UNKNOWN; no auto-retry/reset')
                        time.sleep(.02)
                    rc = channel.recv_exit_status()
                    channel.close()
                    print(out.decode('utf8', errors='replace'), end='', flush=True)
                    print('BOARD_COMMAND_EXIT=' + str(rc), flush=True)
                elif op == 'mkdir':
                    sftp.mkdir(v['remote'], mode=0o700)
                    print('BOARD_DIRECTORY_CREATED', flush=True)
                elif op == 'put':
                    local = Path(v['local'])
                    if local.is_symlink() or not local.is_file():
                        raise ValueError('regular local source required')
                    with sftp.open(v['remote'], 'wx') as dest, local.open('rb') as src:
                        dest.set_pipelined(True)
                        sent = 0
                        while True:
                            data = src.read(1024 * 1024)
                            if not data:
                                break
                            dest.write(data)
                            sent += len(data)
                            if sent % (8 * 1024 * 1024) == 0:
                                print('BOARD_UPLOAD_BYTES=' + str(sent), flush=True)
                    print('BOARD_UPLOADED ' + str(local.name), flush=True)
                elif op == 'get':
                    local = Path(v['local'])
                    with local.open('xb') as dest, sftp.open(v['remote'], 'rb') as src:
                        while True:
                            data = src.read(1024 * 1024)
                            if not data:
                                break
                            dest.write(data)
                    print('BOARD_DOWNLOADED ' + local.name, flush=True)
                else:
                    raise ValueError('unknown operator request')
            except Exception as e:
                print('BOARD_OPERATION_ERROR ' + type(e).__name__ + ': ' + str(e), flush=True)
    finally:
        sftp.close()
        client.close()


if __name__ == '__main__':
    main()
