#!/usr/bin/env python3
"""Screenshot a running Pebble emulator via QEMU's own monitor socket.

`pebble screenshot` goes through pypkjs, which wedges permanently the first
time a button is pressed. The QEMU monitor is a separate path and keeps
working. Usage: shot.py <platform>[@sdk] <out.png>

Colours are rescaled to true 0..255 (see rescale()).
"""
import re, socket, subprocess, sys, time, os

def monitor_port(platform):
    """`platform` is e.g. `gabbro`, or `gabbro@4.17` to pick the emulator of a
    particular SDK when more than one instance of a platform is running (each
    SDK keeps its own flash image, so a second SDK gives an isolated watch)."""
    plat, _, sdk = platform.partition('@')
    ps = subprocess.run(['ps', 'ax', '-o', 'command'], capture_output=True, text=True).stdout
    for line in ps.splitlines():
        if 'qemu-pebble' not in line or plat not in line:
            continue
        if sdk and f'Pebble SDK/{sdk}/{plat}/' not in line:
            continue
        m = re.search(r'-monitor tcp::(\d+)', line)
        if m:
            return int(m.group(1))
    raise SystemExit(f'no running qemu-pebble for {platform}')

# Value each emulator's screendump uses for a full-on channel, measured
# 2026-10-06. Chalk's dump is also interpolated, so it holds in-between values
# too; snapping cleans those up.
FULL = {'emery': 100, 'gabbro': 180, 'basalt': 170, 'chalk': 170}


def full_scale(levels, platform=None):
    """Full scale for a dump: the measured value for known emulators,
    otherwise the brightest level present (black-and-white emulators only ever
    write black and full white)."""
    if platform in FULL:
        return FULL[platform]
    return levels[-1]


def snap_table(full):
    """Maps each dumped value to the nearest legal channel level, 0/85/170/255."""
    return bytes(min(255, round(v * 3 / full) * 85) for v in range(256))


def rescale(ppm, platform=None):
    """The dump claims maxval 255 but uses a smaller per-platform scale, so
    captures look dim and muddy. Snap every channel back to the four levels
    the watch can actually show."""
    data = open(ppm, 'rb').read()
    parts = data.split(b'\n', 3)
    if parts[0] != b'P6' or len(parts) < 4:
        return
    pixels = parts[3]
    levels = sorted(set(pixels) - {0})
    if not levels:
        return
    table = snap_table(full_scale(levels, platform))
    with open(ppm, 'wb') as f:
        f.write(b'\n'.join(parts[:3]) + b'\n' + pixels.translate(table))


def main():
    platform, out = sys.argv[1], sys.argv[2]
    ppm = '/tmp/pebble_shot.ppm'
    if os.path.exists(ppm):
        os.remove(ppm)
    s = socket.create_connection(('127.0.0.1', monitor_port(platform)), timeout=5)
    time.sleep(0.3)
    s.recv(65536)
    s.sendall(f'screendump {ppm}\n'.encode())
    time.sleep(0.8)
    s.close()
    if not os.path.exists(ppm):
        raise SystemExit('screendump produced no file')
    rescale(ppm, platform.partition('@')[0])
    subprocess.run(['sips', '-s', 'format', 'png', ppm, '--out', out],
                   check=True, capture_output=True)
    print(out)

if __name__ == '__main__':
    main()
