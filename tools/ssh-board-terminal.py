#!/usr/bin/env python3
"""A733 test terminal: prompt BEFORE connecting; never persist the password.

Uses Python's standard PTY support and the installed OpenSSH client, with strict
host-key verification against the user's already trusted known_hosts file.
Run under Linux/WSL with a terminal. stdout may be piped to tee for test evidence.
"""
import argparse
import getpass
import os
import pty
import select
import signal
import sys
import termios
import tty


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("host")
    parser.add_argument("--user", default="openvela")
    parser.add_argument("--known-hosts", required=True)
    parser.add_argument("--cipher", choices=["aes128-gcm@openssh.com", "aes256-gcm@openssh.com", "aes128-ctr"])
    args = parser.parse_args()
    secret = getpass.getpass("Board SSH password (not logged; connect starts afterward): ")
    pid, master = pty.fork()
    if pid == 0:
        ssh_args = [
            "ssh", "-tt", "-o", "ConnectTimeout=8",
            "-o", "StrictHostKeyChecking=yes",
            "-o", "UserKnownHostsFile=" + args.known_hosts,
            "-o", "PreferredAuthentications=password",
            "-o", "PubkeyAuthentication=no",
            "-o", "NumberOfPasswordPrompts=1",
        ]
        if args.cipher:
            ssh_args += ["-c", args.cipher]
        ssh_args.append(args.user + "@" + args.host)
        os.execvp("ssh", ssh_args)
    saved = None
    recent = b""
    sent = False
    try:
        if sys.stdin.isatty():
            saved = termios.tcgetattr(sys.stdin.fileno())
            tty.setraw(sys.stdin.fileno())
        while True:
            readable, _, _ = select.select([master, sys.stdin.fileno()], [], [], 1)
            if master in readable:
                try:
                    data = os.read(master, 65536)
                except OSError:
                    break
                if not data:
                    break
                sys.stdout.buffer.write(data)
                sys.stdout.buffer.flush()
                recent = (recent + data)[-2048:]
                if not sent and b"password:" in recent.lower():
                    os.write(master, secret.encode("utf-8") + b"\n")
                    secret = None
                    sent = True
                    recent = b""
            if sys.stdin.fileno() in readable:
                data = os.read(sys.stdin.fileno(), 4096)
                if not data:
                    break
                # Do not forward terminal input while authentication is pending.
                if sent:
                    os.write(master, data)
    finally:
        secret = None
        if saved is not None:
            termios.tcsetattr(sys.stdin.fileno(), termios.TCSADRAIN, saved)
        os.close(master)
        # The interactive connection normally exits on the remote `exit` command.
        result, status = os.waitpid(pid, os.WNOHANG)
        if result == 0:
            os.kill(pid, signal.SIGTERM)
            _, status = os.waitpid(pid, 0)
    return os.waitstatus_to_exitcode(status)


if __name__ == "__main__":
    raise SystemExit(main())
