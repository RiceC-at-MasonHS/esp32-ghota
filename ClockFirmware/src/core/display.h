#pragma once
#include <Arduino.h>

// Hardware-specific display API. Exactly one src/variants/<id>/ file
// implements it, selected by the CLOCK_VARIANT_* define in build_info.h.
void display_begin();
void display_showTime(int hour, int minute);
void display_showNoTime(); // time not synced yet
void display_showUpdateProgress(uint8_t percent);
