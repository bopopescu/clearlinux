#!/bin/sh

# Generate some random data and sign it.

LEAF_KEY=leaf.key.pem
LEAF_CERT=leaf.cert.pem
CA_CHAIN_CERT=ca-chain.cert.pem
PASSWORD="correct horse battery staple"

DATA=my-data

# Generate some random data.
openssl rand -out $DATA 100001

# Sign it.
openssl smime -sign \
    -in $DATA -binary \
    -out ${DATA}.signed -outform PEM \
    -md sha256 \
    -inkey $LEAF_KEY \
    -signer $LEAF_CERT \
    -certfile $CA_CHAIN_CERT \
    -passin pass:"$PASSWORD"
