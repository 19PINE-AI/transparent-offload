#!/usr/bin/env bash
# Regenerate the self-signed TEST certificate for the stunnel third-party-binary
# test (stunnel.conf points `cert =` at combined.pem).
# Throwaway localhost cert for local TLS tests only — never production.
set -euo pipefail
cd "$(dirname "$0")"

openssl req -x509 -newkey rsa:2048 -nodes \
  -keyout key.pem -out cert.pem -days 3650 -subj "/CN=localhost"

# stunnel wants the cert and key in one file
cat cert.pem key.pem > combined.pem

echo "wrote cert.pem key.pem combined.pem"
