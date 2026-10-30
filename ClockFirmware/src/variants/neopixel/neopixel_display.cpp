// Display driver for the NeoPixel (WS2812) strip clock.
#include "../../../config.h"
#if defined(CLOCK_VARIANT_NEOPIXEL)

#include "../../core/display.h"

void display_begin()
{
    // TODO: initialise NeoPixel (WS2812) strip hardware.
}

void display_showTime(int hour, int minute)
{
    // TODO: draw the time on the NeoPixel (WS2812) strip.
    Serial.printf("[neopixel] %02d:%02d\n", hour, minute);
}

void display_showNoTime()
{
    // TODO: e.g. dashes or a blinking pattern while waiting for NTP.
    Serial.println("[neopixel] --:--");
}

void display_showUpdateProgress(uint8_t percent)
{
    // TODO: show update progress; runs during the nightly OTA window.
    Serial.printf("[neopixel] updating %u%%\n", percent);
}

#endif
