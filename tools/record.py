#!/usr/bin/env python3
"""Record emulator frames with real timestamps via the QEMU monitor.

Saves rescaled PNG frames plus times.json into <dir>, waiting for each
screendump to finish before asking for the next, so the timestamps are real
and a GIF built from them plays at true speed. Optionally sends a flick
(`pebble emu-tap`) partway through, to film the time forming on cue.

Usage: record.py <platform>[@sdk] <dir> <seconds> [--tap-at S] [--interval S]
"""
import json, os, shutil, socket, subprocess, sys, threading, time

sys.path.insert(0, os.path.dirname(__file__))
from shot import monitor_port, rescale  # noqa: E402


def wait_prompt(s):
    buf = b''
    while b'(qemu)' not in buf:
        chunk = s.recv(65536)
        if not chunk:
            break
        buf += chunk


def main():
    platform, out_dir, seconds = sys.argv[1], sys.argv[2], float(sys.argv[3])
    tap_at = float(sys.argv[sys.argv.index('--tap-at') + 1]) if '--tap-at' in sys.argv else None
    # A dump takes ~2ms, so without pacing this records thousands of frames.
    interval = float(sys.argv[sys.argv.index('--interval') + 1]) if '--interval' in sys.argv else 0.08
    shutil.rmtree(out_dir, ignore_errors=True)
    os.makedirs(out_dir)
    s = socket.create_connection(('127.0.0.1', monitor_port(platform)), timeout=10)
    wait_prompt(s)
    if tap_at is not None:
        def tap():
            time.sleep(tap_at)
            plat, _, sdk = platform.partition('@')
            cmd = ['pebble', 'emu-tap', '--emulator', plat] + (['--sdk', sdk] if sdk else [])
            subprocess.run(cmd, capture_output=True)
        threading.Thread(target=tap, daemon=True).start()
    start = time.time()
    times = []
    i = 0
    while time.time() - start < seconds:
        ppm = os.path.join(out_dir, f'f{i:03d}.ppm')
        t = time.time() - start
        s.sendall(f'screendump {ppm}\n'.encode())
        wait_prompt(s)
        times.append(round(t, 3))
        i += 1
        time.sleep(max(0.0, start + i * interval - time.time()))
    s.close()
    for k in range(i):
        ppm = os.path.join(out_dir, f'f{k:03d}.ppm')
        rescale(ppm, platform.partition('@')[0])
        subprocess.run(['sips', '-s', 'format', 'png', ppm, '--out', ppm[:-4] + '.png'],
                       check=True, capture_output=True)
        os.remove(ppm)
    json.dump(times, open(os.path.join(out_dir, 'times.json'), 'w'))
    print(out_dir, i, 'frames over', round(time.time() - start, 1), 's')


if __name__ == '__main__':
    main()
