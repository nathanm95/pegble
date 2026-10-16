#
# This file is the default set of rules to compile a Pebble application.
#
# Feel free to customize this to your needs.
#
import os.path

top = '.'
out = 'build'


def options(ctx):
    ctx.load('pebble_sdk')


def configure(ctx):
    """
    This method is used to configure your build. ctx.load(`pebble_sdk`) automatically configures
    a build for each valid platform in `targetPlatforms`. Platform-specific configuration: add your
    change after calling ctx.load('pebble_sdk') and make sure to set the correct environment first.
    Universal configuration: add your change prior to calling ctx.load('pebble_sdk').
    """
    ctx.load('pebble_sdk')


def debug_defines():
    """Debug-only switches, read from the environment so a release build never
    carries them. See CLAUDE.md for what each one is for.

      PEGGLE_BOOT=1..6        start on: 1 a level, 2 endless, 3 the daily,
                              4 trophies & stats, 5 the power picker, 6 the
                              paused run (Continue), instead of the menu
      PEGGLE_LEVEL=n          which level BOOT=1 or 5 opens (0-based)
      PEGGLE_AUTOPLAY=1       the game aims and fires by itself, and restarts
                              when a run ends: a demo you can screendump
      PEGGLE_FEVER_DEMO=n     every board keeps n orange pegs (the rest turn
                              blue), so a few good shots reach the fever
      PEGGLE_BOARD=n          endless starts with n boards already cleared
      PEGGLE_HEAP=1           show the lowest free heap seen, in the game
      PEGGLE_DEMO_PROGRESS=1  the menu shows a played-in save (in memory only,
                              nothing is written): for store screenshots
      PEGGLE_BEACON=1         fifteen pixels at the top encode the game state and
                              tick, for tools/store_assets.py (it paints them out)
    """
    defines = []
    if os.environ.get('PEGGLE_BOOT'):
        defines += ['PEGGLE_BOOT=' + os.environ['PEGGLE_BOOT'],
                    'PEGGLE_LEVEL=' + os.environ.get('PEGGLE_LEVEL', '0')]
    if os.environ.get('PEGGLE_AUTOPLAY'):
        defines += ['PEGGLE_AUTOPLAY=1']
    if os.environ.get('PEGGLE_FEVER_DEMO'):
        defines += ['PEGGLE_FEVER_DEMO=' + os.environ['PEGGLE_FEVER_DEMO']]
    if os.environ.get('PEGGLE_BOARD'):
        defines += ['PEGGLE_BOARD=' + os.environ['PEGGLE_BOARD']]
    if os.environ.get('PEGGLE_HEAP'):
        defines += ['PEGGLE_HEAP=1']
    if os.environ.get('PEGGLE_DEMO_PROGRESS'):
        defines += ['PEGGLE_DEMO_PROGRESS=1']
    if os.environ.get('PEGGLE_BEACON'):
        defines += ['PEGGLE_BEACON=1']
    return defines


def build(ctx):
    ctx.load('pebble_sdk')

    build_worker = os.path.exists('worker_src')
    binaries = []

    cached_env = ctx.env
    for platform in ctx.env.TARGET_PLATFORMS:
        ctx.env = ctx.all_envs[platform]
        ctx.env.append_value('DEFINES', debug_defines())
        ctx.set_group(ctx.env.PLATFORM_NAME)
        app_elf = '{}/pebble-app.elf'.format(ctx.env.BUILD_DIR)
        ctx.pbl_build(source=ctx.path.ant_glob('src/c/**/*.c'), target=app_elf, bin_type='app')

        if build_worker:
            worker_elf = '{}/pebble-worker.elf'.format(ctx.env.BUILD_DIR)
            binaries.append({'platform': platform, 'app_elf': app_elf, 'worker_elf': worker_elf})
            ctx.pbl_build(source=ctx.path.ant_glob('worker_src/c/**/*.c'),
                          target=worker_elf,
                          bin_type='worker')
        else:
            binaries.append({'platform': platform, 'app_elf': app_elf})
    ctx.env = cached_env

    ctx.set_group('bundle')
    ctx.pbl_bundle(binaries=binaries,
                   js=ctx.path.ant_glob(['src/pkjs/**/*.js',
                                         'src/pkjs/**/*.json',
                                         'src/common/**/*.js']),
                   js_entry_file='src/pkjs/index.js')
