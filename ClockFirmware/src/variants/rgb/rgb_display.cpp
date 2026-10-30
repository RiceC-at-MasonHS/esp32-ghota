// Display driver for the discrete RGB LEDs clock.
#include "../../../config.h"
#if defined(CLOCK_VARIANT_RGB)

#include "../../core/display.h"

void display_begin()
{
    // TODO: initialise discrete RGB LEDs hardware.
}

void display_showTime(int hour, int minute)
{
    // TODO: draw the time on the discrete RGB LEDs.
    Serial.printf("[rgb] %02d:%02d\n", hour, minute);
}

void display_showNoTime()
{
    // TODO: e.g. dashes or a blinking pattern while waiting for NTP.
    Serial.println("[rgb] --:--");
}

void display_showUpdateProgress(uint8_t percent)
{
    // TODO: show update progress; runs during the nightly OTA window.
    Serial.printf("[rgb] updating %u%%\n", percent);
}

#endif
