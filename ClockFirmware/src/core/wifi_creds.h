#pragma once
#include <Arduino.h>

// Wi-Fi credentials live in NVS so release binaries never contain them.
// On first boot after a USB flash built with secrets.h, they are copied
// into NVS; OTA updates leave NVS untouched.
bool wifiCreds_load(String &ssid, String &password);
void wifiCreds_save(const String &ssid, const String &password);
