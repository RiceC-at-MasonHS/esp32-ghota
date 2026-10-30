#!/usr/bin/env bash
# Create an ECDSA P-256 firmware signing keypair.
#
#   scripts/make_signing_key.sh primary   # -> GitHub secret OTA_SIGNING_KEY
#   scripts/make_signing_key.sh backup    # -> offline storage only
#
# Writes <name>-private.pem (SECRET) and <name>-public.pem into the current
# directory, then prints the public key to paste into ClockFirmware/ota_keys.h.
# Run it somewhere outside the repo; *.pem is git-ignored as a safety net.
set -euo pipefail

name="${1:?usage: $0 primary|backup}"
priv="${name}-private.pem"
pub="${name}-public.pem"

if [[ -e "$priv" ]]; then
    echo "$priv already exists; refusing to overwrite" >&2
    exit 1
fi

umask 077
openssl ecparam -name prime256v1 -genkey -noout -out "$priv"
openssl ec -in "$priv" -pubout -out "$pub" 2>/dev/null

echo "Private key: $PWD/$priv  (keep secret; see docs/GITHUB_SETUP.md)"
echo "Public key:  $PWD/$pub"
echo
echo "Paste into ClockFirmware/ota_keys.h:"
cat "$pub"
