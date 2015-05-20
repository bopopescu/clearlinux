#!/bin/sh

# Verify data generated and signed by sign.sh.

CA_CERT=ca.cert.pem

DATA=my-data

openssl smime -verify \
    -content $DATA \
    -in ${DATA}.signed -inform PEM \
    -out /dev/null \
    -CAfile $CA_CERT
