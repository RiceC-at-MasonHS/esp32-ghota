// Display driver for the 7-segment display clock.
#include "../../../config.h"
#if defined(CLOCK_VARIANT_SEG7)

#include "../../core/display.h"

void display_begin()
{
    // TODO: initialise 7-segment display hardware.
}

void display_showTime(int hour, int minute)
{
    // TODO: draw the time on the 7-segment display.
    Serial.printf("[seg7] %02d:%02d\n", hour, minute);
}

void display_showNoTime()
{
    // TODO: e.g. dashes or a blinking pattern while waiting for NTP.
    Serial.println("[seg7] --:--");
}

void display_showUpdateProgress(uint8_t percent)
{
    // TODO: show update progress; runs during the nightly OTA window.
    Serial.printf("[seg7] updating %u%%\n", percent);
}

#endif
