#pragma once

#include <pebble.h>

// The original Pebble and Pebble Steel (aplite) give an app 24KB for code,
// data and heap together; every watch since has at least 64KB. The full game
// does not fit there, so aplite builds a lighter one:
//
//   kept:    fever, combos and style shots, stars, the daily challenge, endless
//            with its difficulty ramp, saved runs, the five abilities (dealt as
//            a mix of the unlocked ones), effects and haptics, the original
//            six levels
//   dropped: levels 7-12 and the furniture they need (bumpers, moving rows,
//            portals — endless doesn't bring any in either), the power
//            picker, trophies and the stats screen, timeline pins
//
// Everything outside this switch is the same code on every watch.
#if defined(PBL_PLATFORM_APLITE)
#define PEGBLE_LITE 1
#else
#define PEGBLE_LITE 0
#endif
