# GithubReleaseOTA

Unattended, signed over-the-air updates for ESP32 from GitHub Releases.
Built for devices nobody will ever log into: there's no web UI and no
inbound port. Each night the device checks for an update and installs it
only if it's verified.

Requires **arduino-esp32 3.x** (tested with 3.3.12). ESP32 only.

## Usage

```cpp
#include <WiFi.h>
#include <GithubReleaseOTA.h>

GithubReleaseOTA ota("your-org", "clock-firmware", "seg7", FW_VERSION);

void setup() {
  WiFi.begin(ssid, password);
  ota.addPublicKey(OTA_PUBLIC_KEY_PRIMARY);
  ota.addPublicKey(OTA_PUBLIC_KEY_BACKUP);
  ota.setTimezone("EST5EDT,M3.2.0,M11.1.0");
  ota.begin();
}

void loop() {
  ota.handle();
}
```

> **Rollback protection is automatic.** Including this library overrides
> the core's `verifyRollbackLater()` hook, so a new image stays on
> probation until `handle()` confirms it can still reach GitHub. Always
> call `begin()` and `handle()`. Otherwise every OTA image is rolled back
> on its next reboot.

## What it fetches

```
https://github.com/<owner>/<repo>/releases/latest/download/manifest-<variant>.txt
```

```
format=ghota1
variant=seg7
version=1.12.3
size=1048448
sha256=f3ae5a8e...
url=https://github.com/<owner>/<repo>/releases/download/v1.12.3/clock-seg7-1.12.3.bin
sig=MEUCIQ...            base64 DER ECDSA P-256 / SHA-256
```

The signature covers `"ghota1\n<variant>\n<version>\n<size>\n<sha256>\n"`.
The device rejects the update unless:

- a trusted key verifies the signature
- `variant` equals its own
- `version` is newer (semver; it never downgrades)
- `url` is a release asset of the same repo
- the downloaded binary matches `size` and `sha256`

The image is written to the inactive OTA slot and activated only after the
hash matches.

`scripts/ota_release.py sign` produces these files. `release.yml` shows
the full pipeline.

## API

| Call | Default | Notes |
|---|---|---|
| `GithubReleaseOTA(owner, repo, variant, version)` | | Strings must outlive the object. |
| `addPublicKey(pem)` | | Up to 4. Add the primary and the offline backup key. |
| `setTimezone(posixTz)` | `"UTC0"` | Also sets system time via NTP for the sketch. |
| `setUpdateWindow(start, end)` | `2, 5` | Local hours. May wrap midnight. Devices pick a random minute in the first half. |
| `setRetryMinutes(m)` | `20` | Retry interval after network failures inside the window. |
| `setCheckOnBoot(bool)` | `false` | One immediate check after Wi-Fi connects (bench testing). |
| `setHealthTimeoutMinutes(m)` | `15` | Probation for a new image before rollback. |
| `setRootCAs(pem)` | built-in bundle | GitHub + release-asset host roots. |
| `setAllowInsecureFallback(bool)` | `true` | Retry with unverified TLS if the chain fails to verify. |
| `setTimeout(ms)` | `15000` | Per request and per stalled read. |
| `setDebug(bool)` | `false` | `[OTA]` logs on Serial. |
| `onUpdateStart(fn(String))` / `onProgress(fn(done,total))` / `onError(fn(Result,String))` | | Optional. |
| `begin()` / `handle()` | | Required. |
| `checkNow(install=true)` | | Manual check. Doesn't return if it installs. |
| `markHealthy()` | | Ends probation early (normally automatic). |
| `pendingVerify()`, `latestVersion()`, `compareVersions(a,b)` | | Helpers. |

If NTP never syncs, the library falls back to checking once per 24 hours of
uptime, starting one hour after boot.

## Credits

Started from the structure of
[ESPGithubOTA](https://github.com/himanshufanibhare/ESPGithubOTA) by
Himanshu Fanibhare (MIT), rewritten for signed, unattended,
release-based updates.
