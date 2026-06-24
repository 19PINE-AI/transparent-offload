#!/usr/bin/env bash
# Regenerate the self-signed TEST certificates used by the TSan harnesses
# (harness.c, sesscheck*.c, sess_socket_harness.c, dbg.c).
# Throwaway localhost certs for local TLS tests only — never production.
set -euo pipefail
cd "$(dirname "$0")"

# RSA path: cert.pem + key.pem (PKCS#8)
openssl req -x509 -newkey rsa:2048 -nodes \
  -keyout key.pem -out cert.pem -days 3650 -subj "/CN=localhost"

# EC path: eckey.pem (SEC1 "EC PRIVATE KEY") + eccert.pem (prime256v1 / P-256)
openssl ecparam -genkey -name prime256v1 -noout -out eckey.pem
openssl req -x509 -new -key eckey.pem \
  -out eccert.pem -days 3650 -subj "/CN=localhost"

echo "wrote cert.pem key.pem eccert.pem eckey.pem"
