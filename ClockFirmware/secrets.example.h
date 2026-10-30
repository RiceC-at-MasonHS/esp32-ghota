// Copy to secrets.h (git-ignored) for the first USB flash of a clock.
// The credentials are saved to the ESP32's NVS on first boot and survive
// OTA updates, so release binaries never contain Wi-Fi passwords.
#pragma once
#define WIFI_SSID     "your-ssid"
#define WIFI_PASSWORD "your-password"
