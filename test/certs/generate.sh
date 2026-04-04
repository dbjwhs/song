#!/bin/bash
# MIT License
# Copyright (c) 2026 dbjwhs
#
# Generate self-signed test certificates for TLS transport tests.
# These are for testing only -- do not use in production.

set -euo pipefail

DIR="$(cd "$(dirname "$0")" && pwd)"

# CA key and certificate
openssl req -x509 -newkey rsa:2048 -keyout "$DIR/ca_key.pem" \
    -out "$DIR/ca_cert.pem" -days 3650 -nodes \
    -subj "/CN=Song Test CA/O=Song/C=US" 2>/dev/null

# Server key and CSR
openssl req -newkey rsa:2048 -keyout "$DIR/server_key.pem" \
    -out "$DIR/server.csr" -nodes \
    -subj "/CN=localhost/O=Song/C=US" 2>/dev/null

# Sign server cert with CA (add SAN for localhost)
openssl x509 -req -in "$DIR/server.csr" -CA "$DIR/ca_cert.pem" \
    -CAkey "$DIR/ca_key.pem" -CAcreateserial \
    -out "$DIR/server_cert.pem" -days 3650 \
    -extfile <(printf "subjectAltName=DNS:localhost,IP:127.0.0.1") 2>/dev/null

# Client key and CSR
openssl req -newkey rsa:2048 -keyout "$DIR/client_key.pem" \
    -out "$DIR/client.csr" -nodes \
    -subj "/CN=Song Client/O=Song/C=US" 2>/dev/null

# Sign client cert with CA
openssl x509 -req -in "$DIR/client.csr" -CA "$DIR/ca_cert.pem" \
    -CAkey "$DIR/ca_key.pem" -CAcreateserial \
    -out "$DIR/client_cert.pem" -days 3650 2>/dev/null

# Wrong CA (for testing cert verification failure)
openssl req -x509 -newkey rsa:2048 -keyout "$DIR/wrong_ca_key.pem" \
    -out "$DIR/wrong_ca_cert.pem" -days 3650 -nodes \
    -subj "/CN=Wrong CA/O=Untrusted/C=US" 2>/dev/null

# Clean up CSR and serial files
rm -f "$DIR"/*.csr "$DIR"/*.srl

echo "Test certificates generated in $DIR"
