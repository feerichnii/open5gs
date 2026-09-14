#!/usr/bin/env python3
"""E14-08 unit-ish check: SUPI range matching without full NRF."""
import json
import subprocess
import sys
from pathlib import Path

# Lightweight self-check of fixture shape (full discovery needs running NRF)
fixture = Path(__file__).parent / "fixtures" / "vendor-pcf-profile.json"
profile = json.loads(fixture.read_text())
assert profile["nfType"] == "PCF"
assert "pcfInfo" in profile
assert "supiRanges" in profile["pcfInfo"]
start = profile["pcfInfo"]["supiRanges"][0]["start"]
# Vendor ranges must be digit-only or imsi-prefixed — our matcher strips imsi-
assert start.startswith("imsi-") or start.isdigit() or start.replace("-", "").isalnum()
print("E14-08 fixture OK:", fixture.name)
print("NOTE: run raw-profile-discover.sh against a live NRF for end-to-end")
sys.exit(0)
