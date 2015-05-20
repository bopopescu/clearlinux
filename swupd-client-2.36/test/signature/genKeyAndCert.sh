#!/bin/sh

# This was for a first idea about singing, now abandonned.
# Keeping it for reference.

ROOT_CERT=x-ca.cert.pem
CA_CHAIN_CERT=x-ca-chain.cert.pem
LEAF_KEY=x-R0-0.key.pem
LEAF_CERT=x-R0-0.cert.pem

# Generate private leaf key
openssl genpkey -out $LEAF_KEY -text -algorithm RSA -pkeyopt rsa_keygen_bits:2048

# Generate self-signed leaf certificate containing the public key.
openssl req -out $LEAF_CERT -text -new -x509 -key $LEAF_KEY -days 1000

# For testing, the root and ca-chain certs will be copies of the leaf cert.
cp $LEAF_CERT $ROOT_CERT
cp $LEAF_CERT $CA_CHAIN_CERT
