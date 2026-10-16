#!/bin/sh
# Builds the rules (play.c, levels.c, progress.c) for the Mac against the stub
# pebble.h in this folder, then runs tools/host/sim.c. From the repo root:
#
#   tools/host/run.sh test [games]   the full check suite (default 150 games/level)
#   tools/host/run.sh par [games]    star pars for levels.c (default 600 games/level)
#   tools/host/run.sh stats [games]  how often each event happens
#
# PLATFORM=aplite builds the original Pebble's game: 3 balls, six levels, no
# furniture or trophies (see src/c/config.h).
set -e
cd "$(dirname "$0")/../.."
BIN="${TMPDIR:-/tmp}/peggle-sim"
DEFS="-DPBL_PLATFORM_EMERY"
# aplite: the 3-ball limit and the lighter game in src/c/config.h
[ "$PLATFORM" = "aplite" ] && DEFS="-DPBL_PLATFORM_APLITE"
cc -O2 -g -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers \
   $DEFS -Itools/host -Isrc/c \
   tools/host/sim.c tools/host/host.c src/c/play.c src/c/levels.c src/c/progress.c \
   -lm -o "$BIN"
"$BIN" "$@"
