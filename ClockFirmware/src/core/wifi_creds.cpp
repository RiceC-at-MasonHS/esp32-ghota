#include "wifi_creds.h"
#include <Preferences.h>

#if __has_include("../../secrets.h")
#include "../../secrets.h"
#endif

static const char *NVS_NAMESPACE = "wifi";

void wifiCreds_save(const String &ssid, const String &password)
{
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putString("ssid", ssid);
    prefs.putString("pass", password);
    prefs.end();
}

bool wifiCreds_load(String &ssid, String &password)
{
#if defined(WIFI_SSID) && defined(WIFI_PASSWORD)
    // Built locally with secrets.h: it wins, so re-flashing over USB can
    // change a clock's network.
    wifiCreds_save(WIFI_SSID, WIFI_PASSWORD);
#endif

    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    ssid = prefs.getString("ssid", "");
    password = prefs.getString("pass", "");
    prefs.end();
    return !ssid.isEmpty();
}
