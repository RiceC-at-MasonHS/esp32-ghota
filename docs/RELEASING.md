# Releasing firmware

## Versioning: `vMAJOR.MINOR.PATCH`

One version number for the whole project, one GitHub Release per version.

| Bump | When | Example |
|---|---|---|
| MAJOR | Breaking/core behaviour change. Rebuilds **every** variant. | `v2.0.0` |
| MINOR | Change to shared code (`ClockFirmware/*.ino`, `src/core/`, the OTA library, core version). | `v1.13.0` |
| PATCH | Change that only touches one variant's folder. | `v1.12.3` |

The bump is your judgment. The workflow decides **which variants to
rebuild** from the actual diff against the previous release:

- A variant is rebuilt if anything changed outside the *other* variants'
  folders (and outside `docs/`, `README.md`).
- Otherwise its manifest from the previous release is **carried forward**
  unchanged. Those clocks see the same version as before and do nothing.

So after `v1.12.3` (7-segment fix only), the `seg7` clocks run `1.12.3`
while the `rgb` and `neopixel` clocks stay on `1.12.2`. That's expected.
Every release always contains a manifest for every variant.

Never delete old releases: carried-forward manifests point at their
binaries.

## Cutting a release

1. Merge the work into `main` through a reviewed PR.
2. Tag the merge commit and push the tag (only release-tag owners can):
   ```bash
   git switch main && git pull
   git tag v1.12.3
   git push origin v1.12.3
   ```
3. Open **Actions → Release firmware**. `plan` lists what will be rebuilt
   or carried, and `build` compiles those variants.
4. `release` waits for approval. A reviewer from the `production`
   environment (not the person who pushed the tag) checks the plan and the
   diff, then clicks **Review deployments → Approve**.
5. The job signs, verifies every manifest against `ota_keys.h`, and
   publishes the Release.

To force every variant to rebuild (e.g. after bumping the ESP32 core in
`release.yml`): **Actions → Release firmware → Run workflow**, give the
existing tag, and tick *rebuild_all*.

## What the clocks do

1. Between 02:00 and 05:00 local time, each clock picks a random minute in
   the first half of the window and fetches
   `releases/latest/download/manifest-<variant>.txt`.
2. It checks the signature against both public keys and checks that the
   variant matches and the version is newer. Anything else is ignored.
3. It downloads the binary, hashing as it goes, and only switches to the
   new image if the SHA-256 matches the signed manifest.
4. It reboots into the new version. The new image is on probation: it must
   fetch and verify a manifest within 15 minutes, or it rolls back to the
   previous image. A crash or power loss during probation also rolls it
   back. A rolled-back clock tries again the next night.
5. Network failures are retried every 20 minutes until the window closes.
   A bad signature or manifest is not retried until the next night.

`releases/latest` ignores **pre-releases** and **drafts**, so you can stage
a release as a pre-release to test the files without any clock seeing it.

## Testing an update on the bench

1. Flash a bench clock over USB with `FW_VERSION` set to something older
   than the latest release (e.g. `"0.0.0-dev"`), your Wi-Fi `secrets.h`,
   and the Minimal SPIFFS partition scheme.
2. Temporarily add `ota.setCheckOnBoot(true);` in `setup()` (don't commit
   it), open the Serial Monitor at 115200, and watch the `[OTA]` lines.
3. After the reboot, look for `Image marked healthy; rollback cancelled`.

## Troubleshooting (Serial `[OTA]` messages)

| Message | Meaning |
|---|---|
| `NetworkError: Manifest HTTP 404` | No release yet, or that release has no `manifest-<variant>.txt`. |
| `BadSignature` | Manifest not signed by a key in this clock's `ota_keys.h`. |
| `BadManifest: ... variant` | Wrong variant id compiled into this clock. |
| `WARNING: verified TLS connection failed` | GitHub's certificate chain didn't match the bundled roots. The update still verifies the signature. Run `scripts/update_root_cas.py` and release. |
| `rolling back` | The new image couldn't reach GitHub within 15 min. Check Wi-Fi changes in that release. |
