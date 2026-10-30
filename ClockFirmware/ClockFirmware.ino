// Clock firmware — shared by every hardware variant.
// Variant and version come from build_info.h (CI overwrites it at release).
#include <WiFi.h>
#include <GithubReleaseOTA.h>

#include "config.h"
#include "ota_keys.h"
#include "src/core/clock.h"
#include "src/core/display.h"
#include "src/core/wifi_creds.h"

GithubReleaseOTA ota(OTA_REPO_OWNER, OTA_REPO_NAME, CLOCK_VARIANT_ID, FW_VERSION);

void setup()
{
    Serial.begin(115200);
    Serial.printf("\nClock firmware %s, variant %s\n", FW_VERSION, CLOCK_VARIANT_ID);

    display_begin();

    String ssid, password;
    if (wifiCreds_load(ssid, password))
    {
        WiFi.mode(WIFI_STA);
        WiFi.setAutoReconnect(true);
        WiFi.begin(ssid.c_str(), password.c_str());
    }
    else
    {
        Serial.println("No Wi-Fi credentials stored; flash once over USB with secrets.h");
    }

    ota.addPublicKey(OTA_PUBLIC_KEY_PRIMARY);
    ota.addPublicKey(OTA_PUBLIC_KEY_BACKUP);
    ota.setTimezone(CLOCK_TZ);
    ota.setUpdateWindow(OTA_WINDOW_START_HOUR, OTA_WINDOW_END_HOUR);
    ota.onProgress([](size_t done, size_t total)
                   { display_showUpdateProgress(done * 100 / total); });
    ota.setDebug(true);
    ota.begin();
}

void loop()
{
    ota.handle();
    clock_tick();
    delay(10);
}
