// Release builds: CI overwrites this file (see .github/workflows/release.yml).
// Local Arduino IDE builds: pick the hardware variant you are flashing below.
#pragma once

#define FW_VERSION "0.0.0-dev"

// Exactly one of: CLOCK_VARIANT_SEG7, CLOCK_VARIANT_RGB, CLOCK_VARIANT_NEOPIXEL
#define CLOCK_VARIANT_SEG7
