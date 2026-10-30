# Clock firmware

One Arduino sketch for every clock hardware variant (7-segment, RGB LEDs,
NeoPixel), released through GitHub and installed by the clocks themselves.
Each night they check for a signed update and flash it, with no human
involved.

```
tag v1.12.3 ─► GitHub Actions ─► build changed variants ─► reviewer approves
                                                            │
                     signed manifest-<variant>.txt + .bin ◄─┘  GitHub Release
                                                            │
  clock (02:00–05:00) ─► verify signature ─► download ─► check SHA-256 ─► flash
                         └─ new image must reach GitHub in 15 min or it rolls back
```

## Layout

```
ClockFirmware/                 the Arduino sketch (open this in the IDE)
  ClockFirmware.ino            setup()/loop(): Wi-Fi, OTA, clock
  build_info.h                 FW_VERSION + variant (CI overwrites it)
  config.h                     repo owner/name, timezone, update window
  ota_keys.h                   public keys allowed to sign releases
  secrets.example.h            copy to secrets.h for USB flashing (git-ignored)
  src/core/                    shared logic: clock, Wi-Fi credentials, display API
  src/variants/<id>/           one display driver per hardware variant
libraries/GithubReleaseOTA/    the OTA library (signed, nightly, rollback)
variants.json                  variant ids, board FQBNs, per-variant paths
scripts/ota_release.py         plan / build-info / sign / verify (used by CI)
scripts/make_signing_key.sh    create a signing keypair
scripts/update_root_cas.py     refresh the TLS root CA bundle
.github/workflows/release.yml  the release pipeline
docs/GITHUB_SETUP.md           how the repo, rules and keys must be configured
docs/RELEASING.md              versioning, cutting a release, troubleshooting
```

## Working in the Arduino IDE

1. Install the **esp32 by Espressif** boards package, version 3.3.12 (the
   same version as `ESP32_CORE_VERSION` in `release.yml`).
2. **File → Preferences → Sketchbook location** → this repository's folder.
   The IDE then finds `libraries/GithubReleaseOTA` automatically. Restart
   the IDE.
3. Open `ClockFirmware/ClockFirmware.ino`.
4. In `build_info.h`, choose the variant you're flashing.
5. **Tools → Partition Scheme → Minimal SPIFFS (1.9MB APP with OTA/190KB
   SPIFFS)**. This is required and can never change later over the air.

Command-line equivalent:

```bash
arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=min_spiffs \
  --libraries libraries ClockFirmware
```

## Putting a new clock into service

1. Copy `ClockFirmware/secrets.example.h` to `ClockFirmware/secrets.h` and
   enter the Wi-Fi network the clock will live on.
2. Pick the variant in `build_info.h` and flash over USB (partition scheme
   above).
3. On first boot the credentials are saved to the ESP32's NVS. OTA updates
   keep NVS, so release binaries never contain passwords.
4. A USB build reports version `0.0.0-dev`, so the clock installs the latest
   release for its variant on its first night online.

## Adding a hardware variant

1. Add `src/variants/<id>/<id>_display.cpp` implementing `src/core/display.h`,
   wrapped in `#if defined(CLOCK_VARIANT_<ID>)`.
2. Add the `#elif` for it in `config.h` and the comment in `build_info.h`.
3. Add an entry to `variants.json`.
4. Release. A variant with no manifest in the previous release is always
   built.

Keep variant code inside its folder. Anything outside the variant folders
counts as shared, and changing it rebuilds every variant.

## Security in short

- Firmware is accepted only if its manifest is signed (ECDSA P-256) by a
  key in `ota_keys.h`. The binary must match the signed SHA-256, size,
  variant and a **higher** version.
- The signing key is a GitHub *environment* secret, released only to an
  approved run for a `v*` tag on `main`. See
  [docs/GITHUB_SETUP.md](docs/GITHUB_SETUP.md).
- TLS to GitHub is verified against bundled root CAs. If that ever fails,
  it falls back to unverified TLS rather than stranding the fleet, and the
  signature still protects the firmware.
- The clocks run no web server and accept no inbound connections.
