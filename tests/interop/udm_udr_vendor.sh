#!/bin/sh
# E16-07: UDM ↔ vendor UDR interop expectations
set -e
echo "E16-07 checklist:"
echo " 1. UDR returns 404 + cause USER_NOT_FOUND for unknown SUPI auth-subscription"
echo " 2. UDM proxies that cause to AMF/AUSF"
echo " 3. JSON Patch /sequenceNumber on authentication-subscription works"
echo " 4. context-data amf-3gpp-access PUT/PATCH round-trip"
echo "Configure vendor UDR in udm.yaml client.udr and run registration negative path."
exit 0
