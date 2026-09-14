#!/bin/sh
# E16-07: UDM ↔ vendor UDR interop (404 + USER_NOT_FOUND)
set -e
echo "Run with vendor UDR endpoint configured in udm.yaml"
echo "Expect: GET unknown SUPI -> 404 application/problem+json with cause USER_NOT_FOUND"
