#!/bin/bash
# Creates files only in the explicitly selected directory; never changes trust stores.
set -euo pipefail
if [ "$#" -ne 1 ]; then echo "Usage: $0 NEW_IDENTITY_DIRECTORY" >&2; exit 2; fi
IDENTITY_DIR="$1"
if [ -e "$IDENTITY_DIR" ]; then echo "Refusing to overwrite an existing identity directory" >&2; exit 1; fi
umask 077
mkdir -p "$IDENTITY_DIR"
OPENSSL_BIN="${OPENSSL_BIN:-openssl}"
"$OPENSSL_BIN" req -x509 -newkey rsa:3072 -sha256 -nodes -days 3650 \
    -subj '/CN=BetterCast Dedicated Ethernet' -keyout "$IDENTITY_DIR/receiver.key" -out "$IDENTITY_DIR/receiver.crt"
"$OPENSSL_BIN" rand -hex 24 > "$IDENTITY_DIR/receiver.password"
# PKCS#12 legacy PBE is needed by macOS SecPKCS12Import; protected by directory permissions.
# Transport itself uses modern TLS; this does not change the negotiated TLS cipher.
"$OPENSSL_BIN" pkcs12 -export -inkey "$IDENTITY_DIR/receiver.key" -in "$IDENTITY_DIR/receiver.crt" \
    -keypbe PBE-SHA1-3DES -certpbe PBE-SHA1-3DES -macalg sha1 -passout "file:$IDENTITY_DIR/receiver.password" -out "$IDENTITY_DIR/receiver.p12"
"$OPENSSL_BIN" x509 -in "$IDENTITY_DIR/receiver.crt" -noout -fingerprint -sha256
printf 'Select receiver.p12 on the Mac, then copy its displayed SHA-256 fingerprint to Windows. Keep the private files private.\n'
