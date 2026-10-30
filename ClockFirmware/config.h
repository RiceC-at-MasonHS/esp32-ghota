#pragma once
#include "build_info.h"

// ---- Update source ------------------------------------------------
// Public GitHub repository whose Releases hold the signed firmware.
#define OTA_REPO_OWNER "RiceC-at-MasonHS"
#define OTA_REPO_NAME  "esp32-ghota"

// ---- Time / update window -----------------------------------------
// POSIX TZ string for the clocks' location (US Eastern shown).
#define CLOCK_TZ "EST5EDT,M3.2.0,M11.1.0"
#define OTA_WINDOW_START_HOUR 2
#define OTA_WINDOW_END_HOUR   5

// ---- Variant id ---------------------------------------------------
// The id must match the "id" in variants.json; it selects which
// manifest-<id>.txt this clock follows.
#if defined(CLOCK_VARIANT_SEG7)
#define CLOCK_VARIANT_ID "seg7"
#elif defined(CLOCK_VARIANT_RGB)
#define CLOCK_VARIANT_ID "rgb"
#elif defined(CLOCK_VARIANT_NEOPIXEL)
#define CLOCK_VARIANT_ID "neopixel"
#else
#error "Select a hardware variant in build_info.h"
#endif
