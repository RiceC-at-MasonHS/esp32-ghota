// Public keys that may sign firmware releases. Changing this file changes
// who can update every clock in the field — it is CODEOWNERS-protected.
// Generate keys with scripts/make_signing_key.sh (see docs/GITHUB_SETUP.md).
#pragma once

// Primary key: private half lives in the GitHub "production" environment
// secret OTA_SIGNING_KEY.
static const char OTA_PUBLIC_KEY_PRIMARY[] = R"(-----BEGIN PUBLIC KEY-----
REPLACE_WITH_PRIMARY_PUBLIC_KEY
-----END PUBLIC KEY-----
)";

// Backup key: private half kept offline. Used only to sign a release that
// rotates out a lost or leaked primary key.
static const char OTA_PUBLIC_KEY_BACKUP[] = R"(-----BEGIN PUBLIC KEY-----
REPLACE_WITH_BACKUP_PUBLIC_KEY
-----END PUBLIC KEY-----
)";
