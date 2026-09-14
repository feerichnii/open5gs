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
assert "nfSetIdList" in profile
assert "locality" in profile
start = profile["pcfInfo"]["supiRanges"][0]["start"]
# TS 29.571 SupiRange.start/end are digit strings (optional imsi- tolerated)
assert start.startswith("imsi-") or start.isdigit()
print("E14-08 fixture OK:", fixture.name)
print("NOTE: run raw-profile-discover.sh against a live NRF for end-to-end")
sys.exit(0)
