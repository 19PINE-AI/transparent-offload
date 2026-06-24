#!/usr/bin/env bash
# Regenerate the self-signed TEST certificate for the cond-versioning sweep's
# stunnel instance (stunnel.conf points `cert =` at st.pem).
# Throwaway localhost cert for local TLS tests only — never production.
set -euo pipefail
cd "$(dirname "$0")"

openssl req -x509 -newkey rsa:2048 -nodes \
  -keyout st.key -out st.crt -days 3650 -subj "/CN=localhost"

# stunnel wants the key and cert in one file
cat st.key st.crt > st.pem

echo "wrote st.crt st.key st.pem"
