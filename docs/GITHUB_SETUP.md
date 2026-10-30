# GitHub setup: how the release pipeline should be configured

Every clock in the field installs whatever this repository's release
pipeline signs. These settings make sure only a reviewed commit on `main`,
explicitly approved by a named person, can become signed firmware.

Do the steps in order. Anything marked **Required** must be in place before
the first clock leaves the building.

---

## 0. Threat model in one paragraph

Clocks trust exactly two things: the public keys compiled into
`ClockFirmware/ota_keys.h`, and the version number in a signed manifest.
A Wi-Fi network, DNS server or proxy between a clock and GitHub **cannot**
install firmware, because it can't produce a valid signature. The private
signing key lives only in a GitHub Actions environment secret that is
released to a workflow run after a required reviewer approves it. The
settings below protect that secret and the code path that reaches it.

---

## 1. Repository visibility — Required

The repository (or at least the one hosting Releases) must be **public**.
Clocks download `releases/latest/download/...` anonymously. A private
repo would mean embedding a GitHub token in every clock, which is far worse
than public binaries. Release binaries contain no secrets: Wi-Fi credentials
live in each clock's NVS (see the README), not in the firmware.

> If the source must stay private, keep it private and point
> `OTA_REPO_OWNER/OTA_REPO_NAME` at a separate public "releases-only"
> repository that the workflow publishes into. That needs a fine-grained
> token with `contents: write` on just that repo, stored in the same
> `production` environment.

## 2. Account security — Required

- 2FA required for every member: **Organization → Settings → Authentication
  security → Require two-factor authentication**.
- Keep the number of repo/org **admins** small (2–3). Admins can change
  every rule below.
- Prefer passkeys/security keys for admins.

## 3. Protect `main` — Required

**Settings → Rules → Rulesets → New branch ruleset**

| Setting | Value |
|---|---|
| Name | `protect-main` |
| Enforcement | Active |
| Bypass list | *empty* (not even admins) |
| Target branches | Default branch |
| Restrict deletions | ✅ |
| Block force pushes | ✅ |
| Require a pull request before merging | ✅ |
| ↳ Required approvals | 1 (2 once the team is bigger) |
| ↳ Dismiss stale approvals on new commits | ✅ |
| ↳ Require review from Code Owners | ✅ |
| ↳ Require approval of the most recent reviewable push | ✅ |
| Require status checks to pass | optional — add a build check later |

`.github/CODEOWNERS` routes changes to the workflow, scripts, the OTA
library, `variants.json` and **`ota_keys.h`** to the firmware admins.
It lists `@RiceC-at-MasonHS`; add co-maintainers as the team grows.

## 4. Protect release tags — Required

Tags trigger releases, so creating one must be restricted.

**Settings → Rules → Rulesets → New tag ruleset**

| Setting | Value |
|---|---|
| Name | `protect-release-tags` |
| Enforcement | Active |
| Bypass list | the firmware-admins team (these people may cut releases) |
| Target tags | Include by pattern: `v*` |
| Restrict creations | ✅ |
| Restrict updates | ✅ |
| Restrict deletions | ✅ |
| Block force pushes | ✅ |

Result: only bypass-listed people can create `v*` tags, and nobody can move
or delete one afterwards. The workflow also refuses any tag that isn't on
`main`.

## 5. Actions settings — Required

**Settings → Actions → General**

- **Actions permissions:** "Allow *RiceC-at-MasonHS* actions and select non-*RiceC-at-MasonHS*
  actions", and allow: `actions/checkout@*`, `actions/upload-artifact@*`,
  `actions/download-artifact@*`, `arduino/setup-arduino-cli@*`.
- **Workflow permissions:** "Read repository contents" (the release job
  asks for `contents: write` itself).
- **Allow GitHub Actions to create and approve pull requests:** off.
- **Fork pull request workflows:** require approval for all outside
  collaborators.

Optional hardening: pin each `uses:` in `release.yml` to a full commit SHA
instead of `@v4`, and let Dependabot propose updates.

## 6. The `production` environment and signing key — Required

### 6a. Create the environment

**Settings → Environments → New environment → `production`**

| Setting | Value |
|---|---|
| Required reviewers | 1–2 named people (not a large team) |
| Prevent self-review | ✅ (the person who pushed the tag can't approve their own run; turn off only while there is a single maintainer) |
| Wait timer | optional, e.g. 10 min |
| Allow administrators to bypass | ❌ off |
| Deployment branches and tags | **Selected branches and tags** → add tag rule `v*` |

Only workflow jobs that declare `environment: production`, run for a `v*`
tag, **and** get approved by a reviewer can read this environment's secrets.

### 6b. Generate the keys (on a trusted computer, outside the repo)

```bash
mkdir -p ~/clock-keys && cd ~/clock-keys
/path/to/clock-firmware/scripts/make_signing_key.sh primary
/path/to/clock-firmware/scripts/make_signing_key.sh backup
```

Each run writes `<name>-private.pem` (secret) and `<name>-public.pem`.

### 6c. Install the primary key

1. **Settings → Environments → production → Environment secrets → Add
   secret**: name `OTA_SIGNING_KEY`, value = the full contents of
   `primary-private.pem` (including the BEGIN/END lines).
2. Do **not** add it as a repository or organization secret. It must be
   environment-scoped, or the approval gate means nothing.

### 6d. Store the private keys offline

- `primary-private.pem`: keep one copy in the team password manager. You
  need it again only if you have to re-enter the GitHub secret.
- `backup-private.pem`: **never** put it on GitHub. Store it in the password
  manager **and** on a USB stick kept in a locked drawer or safe. Its only
  job is to rescue the fleet if the primary key is lost or leaked.
- Then delete `~/clock-keys`.

> Losing **both** private keys means no clock can be updated again without
> a USB cable. Check the backups once a semester.

### 6e. Put both public keys in the firmware

Paste the base64 body of each `*-public.pem` into
`ClockFirmware/ota_keys.h` (primary first, backup second) and merge that
through a reviewed PR. Every release workflow run checks that each manifest
verifies against these keys before publishing, so a mismatch fails loudly
instead of shipping a release that no clock would accept.

## 7. Partition scheme — Required before the first USB flash

Every clock must be flashed over USB (the first time) with
**Tools → Partition Scheme → "Minimal SPIFFS (1.9MB APP with OTA/190KB SPIFFS)"**.
The `variants.json` FQBNs use the same scheme. The partition table can
**never** be changed over the air. A clock flashed with "Huge APP (no OTA)"
can never update itself.

---

## Key rotation and incident response

**Routine rotation (optional, e.g. yearly):**
1. Generate a new primary key.
2. PR: replace the primary public key in `ota_keys.h` (keep the backup).
3. Release it **signed with the old key** (secret unchanged), and wait until
   the clocks have updated (watch for a few nights).
4. Replace `OTA_SIGNING_KEY` with the new private key.

**Primary key leaked or lost:**
1. Delete the `OTA_SIGNING_KEY` secret immediately.
2. Generate a new primary key. PR: put it in `ota_keys.h` (slot 1, keep the
   backup in slot 2).
3. Temporarily set `OTA_SIGNING_KEY` to the **backup** private key, cut a
   release, then set it to the new primary key and remove the backup from
   GitHub again.
4. Optionally generate a fresh backup key in a later release.

Clocks accept a release signed by *either* key, so step 3 reaches them even
if the old primary key is in an attacker's hands. Note that until the fleet
updates, the leaked key still works. Act quickly, and keep the higher
version number in your release.

**A bad release went out:** clocks never downgrade. Revert the change on
`main` and release a **higher** version (e.g. `v1.12.4`). Clocks whose new
image can't reach GitHub roll back automatically within 15 minutes, so a
release that breaks Wi-Fi heals itself.

---

## Checklist

- [ ] Repo (or releases repo) is public
- [ ] 2FA required org-wide; ≤3 admins
- [ ] `protect-main` branch ruleset, no bypass, code-owner review
- [ ] CODEOWNERS lists every maintainer
- [ ] `protect-release-tags` tag ruleset on `v*`
- [ ] Actions allow-list and read-only default token
- [ ] `production` environment: reviewers, no self-review, no admin bypass, `v*` tags only
- [ ] `OTA_SIGNING_KEY` is an **environment** secret
- [ ] Backup private key offline in two places; not on GitHub
- [ ] Both public keys in `ota_keys.h`, merged via PR
- [ ] Every clock USB-flashed with Minimal SPIFFS partition scheme
