#include "GithubReleaseOTA.h"

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Update.h>
#include <memory>
#include <time.h>
#include <stdarg.h>

#include "esp_ota_ops.h"
#include "esp_random.h"
#include "mbedtls/base64.h"
#include "mbedtls/md.h"
#include "mbedtls/pk.h"

// Manifest format id. Also the first line of the signed message, so a
// signature can never be confused with one made for another purpose.
static const char *MANIFEST_FORMAT = "ghota1";
static const size_t MAX_MANIFEST_BYTES = 2048;
static const size_t DOWNLOAD_BUF_BYTES = 4096;
static const uint32_t HEALTH_RETRY_MS = 60UL * 1000;
static const uint32_t NO_CLOCK_FIRST_MS = 60UL * 60 * 1000;
static const uint32_t NO_CLOCK_EVERY_MS = 24UL * 60 * 60 * 1000;
static const int ERR_TOO_MANY_REDIRECTS = -100;

// Overrides the weak hook in the core's esp32-hal-misc.c so initArduino()
// leaves a new image pending verify; markHealthy() confirms it later.
extern "C" bool verifyRollbackLater() { return true; }

// ----------------------------------------------------------------
//  HTTP helper
//  GitHub answers release downloads with redirects to another host.
//  Redirects are followed by hand so every hop gets a fresh TLS
//  connection to the right host.
// ----------------------------------------------------------------

namespace
{
    struct Fetch
    {
        // Declared client-first so http is destroyed before the client
        // it references.
        std::unique_ptr<WiFiClientSecure> client;
        std::unique_ptr<HTTPClient> http;

        void close()
        {
            if (http)
                http->end();
            http.reset();
            client.reset();
        }
        ~Fetch() { close(); }
    };

    bool isRedirect(int code)
    {
        return code == 301 || code == 302 || code == 303 || code == 307 || code == 308;
    }

    // Returns the HTTP status of the final hop, or a negative HTTPClient error.
    int openUrl(Fetch &f, String url, const char *rootCAs, bool insecureFallback,
                uint32_t timeoutMs, bool &usedInsecure)
    {
        static const char *headerKeys[] = {"Location"};
        usedInsecure = false;

        for (int hop = 0; hop < 5; hop++)
        {
            int code = HTTPC_ERROR_CONNECTION_REFUSED;
            for (int attempt = 0; attempt < 2; attempt++)
            {
                bool insecure = attempt == 1;
                f.close();
                f.client.reset(new WiFiClientSecure());
                f.http.reset(new HTTPClient());

                if (insecure)
                    f.client->setInsecure();
                else
                    f.client->setCACert(rootCAs);
                f.client->setHandshakeTimeout(timeoutMs / 1000);

                f.http->setReuse(false);
                f.http->setConnectTimeout(timeoutMs);
                f.http->setTimeout(timeoutMs);
                f.http->setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
                f.http->setUserAgent("GithubReleaseOTA");
                f.http->collectHeaders(headerKeys, 1);

                if (!f.http->begin(*f.client, url))
                    return HTTPC_ERROR_CONNECTION_REFUSED;
                code = f.http->GET();

                // A TLS-layer error (as opposed to no network at all) means
                // the certificate chain did not verify against our roots.
                char tlsErr[64];
                bool tlsFailed = code == HTTPC_ERROR_CONNECTION_REFUSED &&
                                 f.client->lastError(tlsErr, sizeof(tlsErr)) != 0;
                if (!insecure && tlsFailed && insecureFallback)
                {
                    usedInsecure = true;
                    continue;
                }
                break;
            }

            if (!isRedirect(code))
                return code;

            url = f.http->header("Location");
            if (!url.startsWith("https://"))
                return HTTPC_ERROR_CONNECTION_REFUSED;
        }
        return ERR_TOO_MANY_REDIRECTS;
    }

    void parseVersion(const char *s, long out[3], const char **pre)
    {
        if (*s == 'v' || *s == 'V')
            s++;
        for (int i = 0; i < 3; i++)
        {
            char *end;
            out[i] = strtol(s, &end, 10);
            s = end;
            if (*s == '.')
                s++;
        }
        *pre = (*s == '-') ? s + 1 : nullptr;
    }

    bool isHex64(const String &s)
    {
        if (s.length() != 64)
            return false;
        for (size_t i = 0; i < s.length(); i++)
            if (!isxdigit((unsigned char)s[i]) || isupper((unsigned char)s[i]))
                return false;
        return true;
    }

    bool isVersionString(const String &s)
    {
        if (s.isEmpty() || s.length() > 32)
            return false;
        for (size_t i = 0; i < s.length(); i++)
        {
            char c = s[i];
            if (!isalnum((unsigned char)c) && c != '.' && c != '-' && c != '+')
                return false;
        }
        return true;
    }
}

// ----------------------------------------------------------------
//  Constructor / configuration
// ----------------------------------------------------------------

GithubReleaseOTA::GithubReleaseOTA(const char *owner, const char *repo,
                                   const char *variant, const char *version)
    : _owner(owner),
      _repo(repo),
      _variant(variant),
      _version(version),
      _keys{},
      _keyCount(0),
      _tz("UTC0"),
      _rootCAs(GHOTA_DEFAULT_ROOT_CAS),
      _windowStart(2),
      _windowEnd(5),
      _retryMs(20UL * 60 * 1000),
      _healthTimeoutMs(15UL * 60 * 1000),
      _timeoutMs(15000),
      _checkOnBoot(false),
      _insecureFallback(true),
      _debug(false),
      _begun(false),
      _ntpStarted(false),
      _pendingVerify(false),
      _bootCheckDone(false),
      _bootMs(0),
      _lastAttemptMs(0),
      _attempted(false),
      _doneDay(-1),
      _jitterMin(0)
{
}

void GithubReleaseOTA::addPublicKey(const char *pem)
{
    if (pem && _keyCount < MAX_KEYS)
        _keys[_keyCount++] = pem;
}

void GithubReleaseOTA::setTimezone(const char *posixTz) { _tz = posixTz; }
void GithubReleaseOTA::setRetryMinutes(uint16_t minutes) { _retryMs = (uint32_t)minutes * 60 * 1000; }
void GithubReleaseOTA::setCheckOnBoot(bool enable) { _checkOnBoot = enable; }
void GithubReleaseOTA::setHealthTimeoutMinutes(uint16_t minutes) { _healthTimeoutMs = (uint32_t)minutes * 60 * 1000; }
void GithubReleaseOTA::setRootCAs(const char *pem) { _rootCAs = pem; }
void GithubReleaseOTA::setAllowInsecureFallback(bool enable) { _insecureFallback = enable; }
void GithubReleaseOTA::setTimeout(uint32_t ms) { _timeoutMs = ms; }
void GithubReleaseOTA::setDebug(bool enable) { _debug = enable; }

void GithubReleaseOTA::setUpdateWindow(uint8_t startHour, uint8_t endHour)
{
    _windowStart = startHour % 24;
    _windowEnd = endHour % 24;
}

void GithubReleaseOTA::onUpdateStart(std::function<void(const String &)> cb) { _startCb = cb; }
void GithubReleaseOTA::onProgress(std::function<void(size_t, size_t)> cb) { _progressCb = cb; }
void GithubReleaseOTA::onError(std::function<void(Result, const String &)> cb) { _errorCb = cb; }

// ----------------------------------------------------------------
//  Lifecycle
// ----------------------------------------------------------------

void GithubReleaseOTA::begin()
{
    _begun = true;
    _bootMs = millis();

    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    _pendingVerify = esp_ota_get_state_partition(running, &state) == ESP_OK &&
                     state == ESP_OTA_IMG_PENDING_VERIFY;

    // Spread devices over the first half of the window so they don't all
    // hit GitHub at the same minute, and retries still fit afterwards.
    int windowMin = ((_windowEnd - _windowStart + 24) % 24) * 60;
    if (windowMin == 0)
        windowMin = 24 * 60;
    _jitterMin = esp_random() % (windowMin / 2 + 1);

    _log("Init: %s/%s variant=%s version=%s window=%02u-%02u offset=%umin%s",
         _owner, _repo, _variant, _version, _windowStart, _windowEnd, _jitterMin,
         _pendingVerify ? " (new image, pending verify)" : "");
    if (_keyCount == 0)
        _log("WARNING: no public keys added; updates are disabled");
}

void GithubReleaseOTA::handle()
{
    if (!_begun)
        return;

    bool wifi = WiFi.status() == WL_CONNECTED;
    if (wifi && !_ntpStarted)
    {
        configTzTime(_tz, "pool.ntp.org", "time.google.com", "time.nist.gov");
        _ntpStarted = true;
    }

    uint32_t now = millis();

    // A new image has to prove it can still update itself before it is
    // kept: fetch and verify the manifest within the health timeout.
    if (_pendingVerify)
    {
        if (now - _bootMs >= _healthTimeoutMs)
        {
            _rollback();
            return;
        }
        if (wifi && (!_attempted || now - _lastAttemptMs >= HEALTH_RETRY_MS))
        {
            _attempted = true;
            _lastAttemptMs = now;
            Result r = checkNow(false);
            if (r == Result::UpToDate || r == Result::UpdateAvailable)
            {
                markHealthy();
                int32_t day;
                if (_windowDay(day))
                    _doneDay = day;
            }
        }
        return;
    }

    if (!wifi)
        return;

    if (_checkOnBoot && !_bootCheckDone)
    {
        _bootCheckDone = true;
        _attempted = true;
        _lastAttemptMs = now;
        checkNow(true);
        return;
    }

    _scheduled();
}

void GithubReleaseOTA::markHealthy()
{
    if (!_pendingVerify)
        return;
    if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK)
        _log("Image marked healthy; rollback cancelled");
    _pendingVerify = false;
}

// ----------------------------------------------------------------
//  Scheduling
// ----------------------------------------------------------------

bool GithubReleaseOTA::_timeValid() const
{
    return time(nullptr) > 1700000000; // after Nov 2023 => NTP has synced
}

bool GithubReleaseOTA::_windowDay(int32_t &day) const
{
    if (!_timeValid())
        return false;

    time_t t = time(nullptr);
    struct tm lt;
    localtime_r(&t, &lt);

    int nowMin = lt.tm_hour * 60 + lt.tm_min;
    int startMin = _windowStart * 60;
    int lenMin = ((_windowEnd - _windowStart + 24) % 24) * 60;
    if (lenMin == 0)
        lenMin = 24 * 60;
    int offset = (nowMin - startMin + 24 * 60) % (24 * 60);
    if (offset < _jitterMin || offset >= lenMin)
        return false;

    // Identify the night by the date the window opened, so a window that
    // crosses midnight counts as one night.
    time_t opened = t - (time_t)offset * 60;
    struct tm ot;
    localtime_r(&opened, &ot);
    day = (ot.tm_year + 1900) * 1000 + ot.tm_yday;
    return true;
}

void GithubReleaseOTA::_scheduled()
{
    uint32_t now = millis();

    if (!_timeValid())
    {
        // No clock yet (NTP blocked?): fall back to once a day of uptime.
        if (now - _bootMs < NO_CLOCK_FIRST_MS)
            return;
        if (_attempted && now - _lastAttemptMs < NO_CLOCK_EVERY_MS)
            return;
        _attempted = true;
        _lastAttemptMs = now;
        checkNow(true);
        return;
    }

    int32_t day;
    if (!_windowDay(day) || day == _doneDay)
        return;
    if (_attempted && now - _lastAttemptMs < _retryMs)
        return;

    _attempted = true;
    _lastAttemptMs = now;
    Result r = checkNow(true);

    // Network-type failures retry later in the window; anything else is
    // final for tonight.
    bool retry = r == Result::NoWiFi || r == Result::NetworkError ||
                 r == Result::DownloadFailed || r == Result::HashMismatch ||
                 r == Result::FlashFailed;
    if (!retry)
        _doneDay = day;
}

// ----------------------------------------------------------------
//  Check / verify / install
// ----------------------------------------------------------------

GithubReleaseOTA::Result GithubReleaseOTA::checkNow(bool install)
{
    if (_keyCount == 0 || !_owner || !_repo || !_variant || !_version)
        return _fail(Result::NotConfigured, "No public key or missing owner/repo/variant/version");
    if (WiFi.status() != WL_CONNECTED)
        return _fail(Result::NoWiFi, "WiFi not connected");

    Manifest m;
    Result r = _fetchManifest(m);
    if (r != Result::UpToDate)
        return r;

    if (!_verifySignature(m))
        return _fail(Result::BadSignature, "Manifest signature not valid for any trusted key");

    _latestVersion = m.version;
    int cmp = compareVersions(m.version.c_str(), _version);
    _log("Current %s, latest %s", _version, m.version.c_str());

    if (cmp <= 0)
        return Result::UpToDate;
    if (!install || _pendingVerify)
        return Result::UpdateAvailable;
    return _install(m);
}

// Returns UpToDate on success (meaning "manifest is usable").
GithubReleaseOTA::Result GithubReleaseOTA::_fetchManifest(Manifest &m)
{
    String url = String("https://github.com/") + _owner + "/" + _repo +
                 "/releases/latest/download/manifest-" + _variant + ".txt";
    _log("GET %s", url.c_str());

    Fetch f;
    bool insecure;
    int code = openUrl(f, url, _rootCAs, _insecureFallback, _timeoutMs, insecure);
    if (insecure)
        _log("WARNING: verified TLS connection failed; retried without certificate checks");
    if (code != HTTP_CODE_OK)
        return _fail(Result::NetworkError, "Manifest HTTP " + String(code));

    int len = f.http->getSize();
    if (len > (int)MAX_MANIFEST_BYTES)
        return _fail(Result::BadManifest, "Manifest too large");

    String body = f.http->getString();
    if (body.length() > MAX_MANIFEST_BYTES)
        return _fail(Result::BadManifest, "Manifest too large");

    String err;
    if (!_parseManifest(body, m, err))
        return _fail(Result::BadManifest, err);
    return Result::UpToDate;
}

bool GithubReleaseOTA::_parseManifest(const String &body, Manifest &m, String &err)
{
    String format;
    int pos = 0;
    while (pos < (int)body.length())
    {
        int nl = body.indexOf('\n', pos);
        if (nl < 0)
            nl = body.length();
        String line = body.substring(pos, nl);
        pos = nl + 1;
        line.trim();
        if (line.isEmpty() || line[0] == '#')
            continue;

        int eq = line.indexOf('=');
        if (eq <= 0)
            continue;
        String key = line.substring(0, eq);
        String val = line.substring(eq + 1);

        if (key == "format")
            format = val;
        else if (key == "variant")
            m.variant = val;
        else if (key == "version")
            m.version = val;
        else if (key == "size")
            m.size = strtoul(val.c_str(), nullptr, 10);
        else if (key == "sha256")
            m.sha256 = val;
        else if (key == "url")
            m.url = val;
        else if (key == "sig")
            m.sig = val;
    }

    String urlPrefix = String("https://github.com/") + _owner + "/" + _repo + "/releases/download/";

    if (format != MANIFEST_FORMAT)
        err = "Unknown manifest format '" + format + "'";
    else if (m.variant != _variant)
        err = "Manifest is for variant '" + m.variant + "'";
    else if (!isVersionString(m.version))
        err = "Bad version";
    else if (m.size == 0)
        err = "Bad size";
    else if (!isHex64(m.sha256))
        err = "Bad sha256";
    else if (!m.url.startsWith(urlPrefix))
        err = "Firmware URL is not a release of " + String(_owner) + "/" + _repo;
    else if (m.sig.isEmpty() || m.sig.length() > 128)
        err = "Bad signature field";
    else
        return true;
    return false;
}

bool GithubReleaseOTA::_verifySignature(const Manifest &m)
{
    // Must match scripts/ota_release.py signed_message().
    String msg = String(MANIFEST_FORMAT) + "\n" + m.variant + "\n" + m.version + "\n" +
                 String((unsigned long)m.size) + "\n" + m.sha256 + "\n";

    uint8_t hash[32];
    if (mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
                   (const uint8_t *)msg.c_str(), msg.length(), hash) != 0)
        return false;

    uint8_t sig[96];
    size_t sigLen = 0;
    if (mbedtls_base64_decode(sig, sizeof(sig), &sigLen,
                              (const uint8_t *)m.sig.c_str(), m.sig.length()) != 0)
        return false;

    for (int i = 0; i < _keyCount; i++)
    {
        mbedtls_pk_context pk;
        mbedtls_pk_init(&pk);
        int rc = mbedtls_pk_parse_public_key(&pk, (const uint8_t *)_keys[i], strlen(_keys[i]) + 1);
        if (rc == 0 && mbedtls_pk_can_do(&pk, MBEDTLS_PK_ECDSA))
            rc = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, hash, sizeof(hash), sig, sigLen);
        else if (rc == 0)
            rc = -1;
        mbedtls_pk_free(&pk);

        if (rc == 0)
        {
            _log("Manifest signature OK (key %d)", i);
            return true;
        }
        _log("Key %d did not verify (rc=-0x%04x)", i, (unsigned)-rc);
    }
    return false;
}

GithubReleaseOTA::Result GithubReleaseOTA::_install(const Manifest &m)
{
    _log("Installing %s (%u bytes) from %s", m.version.c_str(), (unsigned)m.size, m.url.c_str());
    if (_startCb)
        _startCb(m.version);

    Fetch f;
    bool insecure;
    int code = openUrl(f, m.url, _rootCAs, _insecureFallback, _timeoutMs, insecure);
    if (insecure)
        _log("WARNING: verified TLS connection failed; retried without certificate checks");
    if (code != HTTP_CODE_OK)
        return _fail(Result::DownloadFailed, "Firmware HTTP " + String(code));

    int len = f.http->getSize();
    if (len >= 0 && (size_t)len != m.size)
        return _fail(Result::DownloadFailed, "Firmware is " + String(len) + " bytes, manifest says " + String((unsigned long)m.size));

    if (!Update.begin(m.size, U_FLASH))
        return _fail(Result::FlashFailed, String("Update.begin: ") + Update.errorString());

    uint8_t *buf = (uint8_t *)malloc(DOWNLOAD_BUF_BYTES);
    if (!buf)
    {
        Update.abort();
        return _fail(Result::FlashFailed, "Out of memory");
    }

    mbedtls_md_context_t md;
    mbedtls_md_init(&md);
    mbedtls_md_setup(&md, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 0);
    mbedtls_md_starts(&md);

    auto *stream = f.http->getStreamPtr();
    size_t written = 0;
    bool writeOk = true;
    uint32_t lastData = millis();
    int lastPct = -1;

    while (written < m.size)
    {
        size_t avail = stream->available();
        if (avail == 0)
        {
            if (!stream->connected() || millis() - lastData > _timeoutMs)
                break;
            delay(1);
            continue;
        }

        size_t want = min(avail, min(DOWNLOAD_BUF_BYTES, m.size - written));
        int n = stream->readBytes(buf, want);
        if (n <= 0)
            continue;
        lastData = millis();

        mbedtls_md_update(&md, buf, n);
        if (Update.write(buf, n) != (size_t)n)
        {
            writeOk = false;
            break;
        }
        written += n;

        int pct = (int)(written * 100 / m.size);
        if (pct != lastPct)
        {
            lastPct = pct;
            if (_progressCb)
                _progressCb(written, m.size);
            if (pct % 10 == 0)
                _log("Progress %d%%", pct);
        }
    }

    free(buf);
    uint8_t digest[32];
    mbedtls_md_finish(&md, digest);
    mbedtls_md_free(&md);

    if (!writeOk)
    {
        String e = String("Update.write: ") + Update.errorString();
        Update.abort();
        return _fail(Result::FlashFailed, e);
    }
    if (written != m.size)
    {
        Update.abort();
        return _fail(Result::DownloadFailed, "Download stopped at " + String((unsigned long)written) + " bytes");
    }

    char hex[65];
    for (int i = 0; i < 32; i++)
        sprintf(hex + i * 2, "%02x", digest[i]);
    if (m.sha256 != hex)
    {
        Update.abort();
        return _fail(Result::HashMismatch, "SHA-256 mismatch; image discarded");
    }

    if (!Update.end())
        return _fail(Result::FlashFailed, String("Update.end: ") + Update.errorString());

    _log("Update to %s installed; restarting", m.version.c_str());
    delay(500);
    ESP.restart();
    return Result::UpToDate; // not reached
}

void GithubReleaseOTA::_rollback()
{
    _log("New image did not verify a manifest within %lu min; rolling back",
         (unsigned long)(_healthTimeoutMs / 60000));
    esp_ota_mark_app_invalid_rollback_and_reboot(); // returns only on failure
    _log("Rollback impossible (no previous image); keeping this one");
    markHealthy();
}

GithubReleaseOTA::Result GithubReleaseOTA::_fail(Result r, const String &msg)
{
    _log("%s: %s", resultName(r), msg.c_str());
    if (_errorCb)
        _errorCb(r, msg);
    return r;
}

void GithubReleaseOTA::_log(const char *fmt, ...)
{
    if (!_debug)
        return;
    char line[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    Serial.print("[OTA] ");
    Serial.println(line);
}

// ----------------------------------------------------------------
//  Static helpers
// ----------------------------------------------------------------

int GithubReleaseOTA::compareVersions(const char *a, const char *b)
{
    long va[3], vb[3];
    const char *pa, *pb;
    parseVersion(a, va, &pa);
    parseVersion(b, vb, &pb);

    for (int i = 0; i < 3; i++)
        if (va[i] != vb[i])
            return va[i] < vb[i] ? -1 : 1;

    // A pre-release sorts before the plain release (1.2.0-rc1 < 1.2.0).
    if (!pa && !pb)
        return 0;
    if (!pa)
        return 1;
    if (!pb)
        return -1;
    int c = strcmp(pa, pb);
    return (c > 0) - (c < 0);
}

const char *GithubReleaseOTA::resultName(Result r)
{
    switch (r)
    {
    case Result::UpToDate: return "UpToDate";
    case Result::UpdateAvailable: return "UpdateAvailable";
    case Result::NoWiFi: return "NoWiFi";
    case Result::NotConfigured: return "NotConfigured";
    case Result::NetworkError: return "NetworkError";
    case Result::BadManifest: return "BadManifest";
    case Result::BadSignature: return "BadSignature";
    case Result::DownloadFailed: return "DownloadFailed";
    case Result::HashMismatch: return "HashMismatch";
    case Result::FlashFailed: return "FlashFailed";
    }
    return "Unknown";
}
