#pragma once
#include <Arduino.h>
#include <functional>

#if !defined(ESP32)
#error "GithubReleaseOTA supports ESP32 boards only."
#endif

// ================================================================
//  GithubReleaseOTA
//  Unattended, signed OTA updates for ESP32 from GitHub Releases.
//
//  Each release publishes one small manifest per hardware variant:
//
//    https://github.com/<owner>/<repo>/releases/latest/download/manifest-<variant>.txt
//
//  The device checks it once a night (inside an update window, with a
//  random per-device offset), verifies the manifest's ECDSA P-256
//  signature against public keys compiled into the firmware, downloads
//  the binary, checks its SHA-256, flashes it and restarts. A freshly
//  installed image must prove it can still reach GitHub before it is
//  marked good; otherwise the ESP32 rolls back to the previous image.
//
//  Minimal sketch:
//
//    #include <WiFi.h>
//    #include <GithubReleaseOTA.h>
//
//    GithubReleaseOTA ota("my-org", "clock-firmware", "seg7", "1.4.2");
//
//    void setup() {
//      WiFi.begin(ssid, pass);
//      ota.addPublicKey(OTA_PUBLIC_KEY_PRIMARY);
//      ota.begin();
//    }
//    void loop() { ota.handle(); }
// ================================================================

// Rollback protection: this library overrides the core's weak
// verifyRollbackLater() hook, so a newly flashed image stays "pending
// verify" until begin()/handle() confirm it can still reach GitHub.
// Any sketch that includes this library must therefore call begin() and
// handle(), or every new image is rolled back on its next reboot.

// Root CAs for github.com and release-assets.githubusercontent.com.
// Defined in GithubReleaseOTA_RootCAs.cpp.
extern const char GHOTA_DEFAULT_ROOT_CAS[];

class GithubReleaseOTA
{
public:
    enum class Result
    {
        UpToDate,        // remote version <= current version
        UpdateAvailable, // newer, valid release found (check-only mode)
        NoWiFi,
        NotConfigured,   // missing public key or bad constructor args
        NetworkError,    // could not fetch the manifest
        BadManifest,     // manifest missing fields or malformed
        BadSignature,    // no trusted key verified the manifest
        DownloadFailed,  // binary download failed or was truncated
        HashMismatch,    // binary did not match the signed SHA-256
        FlashFailed,     // Update library rejected the image
    };

    // owner/repo: the public GitHub repository that hosts the releases.
    // variant:    this hardware variant's id, e.g. "seg7".
    // version:    the version compiled into this firmware, e.g. "1.12.3".
    // All strings must outlive this object (string literals are ideal).
    GithubReleaseOTA(const char *owner, const char *repo,
                     const char *variant, const char *version);

    // ==============================================================
    //  Configuration — call before begin()
    // ==============================================================

    // Trust a PEM-encoded ECDSA P-256 public key. Add the primary and
    // the offline backup key. Up to 4 keys.
    void addPublicKey(const char *pem);

    // POSIX TZ string used for the update window, e.g.
    // "EST5EDT,M3.2.0,M11.1.0". Default: "UTC0".
    void setTimezone(const char *posixTz);

    // Local hours [startHour, endHour) during which updates may run.
    // The window may cross midnight (e.g. 23, 4). Default: 2, 5.
    void setUpdateWindow(uint8_t startHour, uint8_t endHour);

    // Minutes between retries inside the window after a network
    // failure. Default: 20.
    void setRetryMinutes(uint16_t minutes);

    // Check (and install) once shortly after WiFi first connects,
    // ignoring the window. Default: false.
    void setCheckOnBoot(bool enable);

    // How long a newly installed image has to reach GitHub and verify
    // a manifest before it is rolled back. Default: 15 minutes.
    void setHealthTimeoutMinutes(uint16_t minutes);

    // Replace the built-in root CA bundle (concatenated PEM).
    void setRootCAs(const char *pem);

    // If TLS verification fails (e.g. GitHub moved to a CA not in the
    // bundle), retry without certificate checks. Firmware is still
    // protected by the manifest signature and SHA-256, so this trades
    // transport privacy for not stranding devices. Default: true.
    void setAllowInsecureFallback(bool enable);

    // Network timeout per request / per stalled read. Default: 15000 ms.
    void setTimeout(uint32_t ms);

    // Print [OTA] logs to Serial. Default: false.
    void setDebug(bool enable);

    // ==============================================================
    //  Callbacks — optional
    // ==============================================================

    // Called right before a verified update starts downloading.
    void onUpdateStart(std::function<void(const String &newVersion)> cb);

    // Called during download with bytes written and total bytes.
    void onProgress(std::function<void(size_t done, size_t total)> cb);

    // Called whenever a check finishes with an error result.
    void onError(std::function<void(Result result, const String &message)> cb);

    // ==============================================================
    //  Lifecycle
    // ==============================================================

    // Call once in setup(), after WiFi.begin().
    void begin();

    // Call every loop(). Cheap when nothing is due.
    void handle();

    // Run a check right now, outside the schedule. With install=true a
    // newer verified release is flashed and the device restarts (this
    // call does not return in that case).
    Result checkNow(bool install = true);

    // Mark the running image as good, cancelling rollback. Called
    // automatically once the image has verified a manifest.
    void markHealthy();

    // True while a freshly installed image is still on probation.
    bool pendingVerify() const { return _pendingVerify; }

    // Version from the last manifest that verified, or "" if none yet.
    const String &latestVersion() const { return _latestVersion; }

    const char *currentVersion() const { return _version; }
    const char *variant() const { return _variant; }

    // Semantic version compare: <0 if a<b, 0 if equal, >0 if a>b.
    // Accepts an optional leading 'v' and a "-prerelease" suffix.
    static int compareVersions(const char *a, const char *b);

    static const char *resultName(Result r);

private:
    static const int MAX_KEYS = 4;

    struct Manifest
    {
        String variant, version, url, sha256, sig;
        size_t size = 0;
    };

    // Config
    const char *_owner;
    const char *_repo;
    const char *_variant;
    const char *_version;
    const char *_keys[MAX_KEYS];
    int _keyCount;
    const char *_tz;
    const char *_rootCAs;
    uint8_t _windowStart, _windowEnd;
    uint32_t _retryMs;
    uint32_t _healthTimeoutMs;
    uint32_t _timeoutMs;
    bool _checkOnBoot;
    bool _insecureFallback;
    bool _debug;

    // State
    bool _begun;
    bool _ntpStarted;
    bool _pendingVerify;
    bool _bootCheckDone;
    uint32_t _bootMs;
    uint32_t _lastAttemptMs;
    bool _attempted;
    int32_t _doneDay;      // window "day" already finished successfully
    uint16_t _jitterMin;   // per-device offset into the window
    String _latestVersion;

    std::function<void(const String &)> _startCb;
    std::function<void(size_t, size_t)> _progressCb;
    std::function<void(Result, const String &)> _errorCb;

    // Internal
    void _scheduled();
    Result _fail(Result r, const String &msg);
    bool _timeValid() const;
    bool _windowDay(int32_t &day) const;
    Result _fetchManifest(Manifest &m);
    bool _parseManifest(const String &body, Manifest &m, String &err);
    bool _verifySignature(const Manifest &m);
    Result _install(const Manifest &m);
    void _rollback();
    void _log(const char *fmt, ...);
};
