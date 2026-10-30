// Minimal unattended OTA: checks GitHub Releases nightly between 2-5 AM
// local time and installs signed updates for this hardware variant.
#include <WiFi.h>
#include <GithubReleaseOTA.h>

// Paste the output of scripts/make_signing_key.sh here.
static const char PUBLIC_KEY[] = R"(-----BEGIN PUBLIC KEY-----
REPLACE_ME
-----END PUBLIC KEY-----
)";

GithubReleaseOTA ota("your-org", "clock-firmware", "seg7", "1.0.0");

void setup()
{
    Serial.begin(115200);
    WiFi.mode(WIFI_STA);
    WiFi.begin("your-ssid", "your-password");

    ota.addPublicKey(PUBLIC_KEY);
    ota.setTimezone("EST5EDT,M3.2.0,M11.1.0");
    ota.setDebug(true);
    ota.begin();
}

void loop()
{
    ota.handle();
}
