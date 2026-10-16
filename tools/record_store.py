#!/usr/bin/env python3
"""Record everything tools/store_assets.py needs, one emulator at a time.

Builds three debug variants of the game (see the PEGGLE_* flags in wscript),
each with PEGGLE_BEACON so the recording carries the game state:

  demo    level 1 playing itself with three orange pegs left, through a
          fever to the results panel                  -> GIF, fever, clear
  level   a new level playing itself (Pinball; Ring Cycle on aplite, which
          only has the original six)                 -> level still
  menu    the menu over a played-in save              -> menu still

Each is built as a *watchface* (package.json is put back afterwards, whatever
happens): `pebble install` only brings a watchapp to the front on a fresh
emulator, but always switches to a newly installed watchface. Then, per
platform, installs each variant and records it with tools/record.py into
<rec_dir>/<platform>_<variant>/. Runs `pebble kill` between platforms because
the Mac is short of memory, so make sure no other session is using emulators.

Usage: record_store.py <rec_dir> [platform ...] [--only variant,...] [--seconds N]

--seconds overrides how long each variant records: the demo picks its best run
(most stars) from everything it filmed, so a longer recording gives it more to
choose from.
"""
import json, os, shutil, subprocess, sys, time

TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS)
SDK = '4.33.1'
PLATFORMS = ['aplite', 'basalt', 'chalk', 'diorite', 'emery', 'flint', 'gabbro']
LITE = {'aplite'}

COMMON = {'PEGGLE_BEACON': '1', 'PEGGLE_DEMO_PROGRESS': '1'}
# name: (build flags, seconds to record, frame interval). The demo dumps faster
# than the game's 33ms tick so every frame it draws is caught at least once;
# store_assets.py drops repeats and times the GIF by game frames.
VARIANTS = {
    # 60s: a slow emulator (basalt ran at about a third speed) still gets a
    # whole run in.
    'demo': ({'PEGGLE_BOOT': '1', 'PEGGLE_LEVEL': '0', 'PEGGLE_AUTOPLAY': '1',
              'PEGGLE_FEVER_DEMO': '3'}, 60, 0.02),
    'level': ({'PEGGLE_BOOT': '1', 'PEGGLE_LEVEL': '7', 'PEGGLE_AUTOPLAY': '1'}, 8, 0.1),
    'level_lite': ({'PEGGLE_BOOT': '1', 'PEGGLE_LEVEL': '2', 'PEGGLE_AUTOPLAY': '1'}, 8, 0.1),
    'menu': ({}, 3, 0.5),
}


def variants_for(platform, names):
    out = []
    for n in names:
        if n == 'level':
            n = 'level_lite' if platform in LITE else 'level'
        out.append(n)
    return out


def pebble(*args, timeout=240):
    env = dict(os.environ, PATH=os.path.expanduser('~/.local/bin') + ':' + os.environ['PATH'])
    try:
        r = subprocess.run(['pebble', *args], capture_output=True, text=True, timeout=timeout,
                           env=env, cwd=ROOT)
        return r.returncode == 0, r.stdout + r.stderr
    except subprocess.TimeoutExpired:
        return False, 'timeout'


def build_variants(out, names):
    pkg = os.path.join(ROOT, 'package.json')
    original = open(pkg).read()
    paths = {}
    try:
        meta = json.loads(original)
        meta['pebble']['watchapp']['watchface'] = True
        with open(pkg, 'w') as f:
            json.dump(meta, f, indent=2)
        for name in names:
            env = {k: v for k, v in os.environ.items() if not k.startswith('PEGGLE_')}
            env.update(COMMON, **VARIANTS[name][0])
            env['PATH'] = os.path.expanduser('~/.local/bin') + ':' + env['PATH']
            r = subprocess.run(['pebble', 'build'], capture_output=True, text=True, env=env,
                               cwd=ROOT)
            if r.returncode:
                sys.exit('build failed for %s:\n%s' % (name, r.stdout[-2000:]))
            paths[name] = os.path.join(out, 'peggle_%s.pbw' % name)
            shutil.copy(os.path.join(ROOT, 'build', 'peggle.pbw'), paths[name])
            print('built', name, flush=True)
    finally:
        with open(pkg, 'w') as f:
            f.write(original)
    return paths


def on_screen(platform, variant):
    """Is it really Peggle on the screen? A face that was animating when the
    install landed can hang on, so look: the game variants carry a readable
    beacon (store_assets.py), the menu is mostly white."""
    sys.path.insert(0, TOOLS)
    from PIL import Image
    import store_assets
    shot = os.path.join(TOOLS, '..', 'build', 'onscreen.png')
    if os.path.exists(shot):
        os.remove(shot)
    subprocess.run(['python3', os.path.join(TOOLS, 'shot.py'), '%s@%s' % (platform, SDK), shot],
                   capture_output=True)
    if not os.path.exists(shot):
        return False
    im = Image.open(shot).convert('RGB')
    w, h = store_assets.NATIVE[platform]
    if im.size != (w, h):
        x, y = (im.width - w) // 2, (im.height - h) // 2
        im = im.crop((x, y, x + w, y + h))
    if variant == 'menu':
        white = sum(1 for p in im.get_flattened_data() if p == (255, 255, 255))
        return white > w * h * 0.3
    bx, by = store_assets.beacon_xy(platform)
    px = [im.getpixel((bx + k, by)) for k in range(store_assets.BEACON_BITS)]
    if not all(p in ((0, 0, 0), (255, 255, 255)) for p in px):
        return False
    bits = sum(1 << k for k, p in enumerate(px) if p == (255, 255, 255))
    return (bits & 7) <= 4 and (bits >> 12) == (bits & 7)


def install(platform, pbw, variant):
    for attempt in range(4):
        ok, out = pebble('install', '--emulator', platform, '--sdk', SDK, pbw)
        if ok:
            time.sleep(3)
            if on_screen(platform, variant):
                return True
            print('  something else is on screen, dismissing and retrying', flush=True)
        else:
            print('  install failed (%s), retrying' % out.strip().splitlines()[-1][:80],
                  flush=True)
        # A face that won't let go shows "<App> is not responding"; Back clears it.
        pebble('emu-button', '--emulator', platform, '--sdk', SDK, 'click', 'back')
        time.sleep(5)
    return False


def main():
    args = sys.argv[1:]
    names = ['demo', 'level', 'menu']
    seconds_override = None
    if '--seconds' in args:
        k = args.index('--seconds')
        seconds_override = float(args[k + 1])
        del args[k:k + 2]
    if '--only' in args:
        k = args.index('--only')
        names = args[k + 1].split(',')
        del args[k:k + 2]
    rec = os.path.abspath(args[0])
    platforms = args[1:] or PLATFORMS
    os.makedirs(rec, exist_ok=True)
    needed = sorted({v for p in platforms for v in variants_for(p, names)})
    pbws = build_variants(rec, needed)
    for platform in platforms:
        print(platform, flush=True)
        for variant in variants_for(platform, names):
            _, seconds, interval = VARIANTS[variant]
            seconds = seconds_override or seconds
            if not install(platform, pbws[variant], variant.replace('_lite', '')):
                print('  FAILED to install %s' % variant, flush=True)
                continue
            d = os.path.join(rec, '%s_%s' % (platform, variant.replace('_lite', '')))
            subprocess.run(['python3', os.path.join(TOOLS, 'record.py'), '%s@%s' % (platform, SDK),
                            d, str(seconds), '--interval', str(interval)], check=True)
        pebble('kill')
        time.sleep(3)


if __name__ == '__main__':
    main()
