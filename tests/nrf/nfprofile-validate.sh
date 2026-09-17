#!/usr/bin/env bash
# NRF NFProfile validation regression (TS 29.510 hardening).
#
# Offline: validates fixture shapes / expected reject reasons via python.
# Live:    NRF_URL=http://127.0.0.1:7777 ./tests/nrf/nfprofile-validate.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
NRF_URL="${NRF_URL:-}"
CRASH_FIXTURE="$ROOT/tests/nrf/fixtures/chf-nexign-crash-profile.json"
VALID_FIXTURE="$ROOT/tests/nrf/fixtures/chf-valid-profile.json"
NF_ID="97986d02-4c4d-480d-b0e3-87de526bda20"

die() { echo "FAIL: $*" >&2; exit 1; }
ok() { echo "OK: $*"; }

command -v python3 >/dev/null || die "python3 required"

echo "== Offline fixture contract =="
python3 - <<PY
import json
from pathlib import Path
root = Path("$ROOT")
crash = json.loads((root/"tests/nrf/fixtures/chf-nexign-crash-profile.json").read_text())
valid = json.loads((root/"tests/nrf/fixtures/chf-valid-profile.json").read_text())

assert "ipv4Addresses" not in crash and "fqdn" not in crash
assert not crash.get("ipv6Addresses")
# map keys != serviceInstanceId
for k, v in crash["nfServiceList"].items():
    assert k != v["serviceInstanceId"], k
ids = [v["serviceInstanceId"] for v in crash["nfServiceList"].values()]
assert len(ids) != len(set(ids)), "expected duplicate serviceInstanceId"
assert any(not (ver.get("apiFullVersion") or "") for svc in crash["nfServiceList"].values() for ver in svc["versions"])

assert valid["ipv4Addresses"]
for k, v in valid["nfServiceList"].items():
    assert k == v["serviceInstanceId"]
    assert all(ver.get("apiFullVersion") for ver in v["versions"])
ids = [v["serviceInstanceId"] for v in valid["nfServiceList"].values()]
assert len(ids) == len(set(ids))
print("fixture contract ok")
PY
ok "offline fixtures"

if [[ -z "$NRF_URL" ]]; then
  ok "NRF_URL unset — skipping live HTTP"
  echo "ALL CHECKS PASSED (offline)"
  exit 0
fi

curl_nrf() {
  curl --http2-prior-knowledge -sS "$@"
}

echo "== Crash profile must 400 (NRF stays accepting) =="
code=$(curl_nrf -o /tmp/nrf-chf-bad.json -w '%{http_code}' \
  -X PUT "${NRF_URL}/nnrf-nfm/v1/nf-instances/${NF_ID}" \
  -H 'Content-Type: application/json' \
  --data-binary @"$CRASH_FIXTURE")
[[ "$code" == "400" ]] || die "crash profile want 400 got $code: $(cat /tmp/nrf-chf-bad.json)"
ok "crash profile → 400"

echo "== Rejected NF must not appear in discovery =="
disc=$(curl_nrf "${NRF_URL}/nnrf-disc/v1/nf-instances?target-nf-type=CHF&requester-nf-type=SMF" || true)
echo "$disc" | python3 -c 'import json,sys; d=json.load(sys.stdin); ids=[x.get("nfInstanceId") for x in (d.get("nfInstances") or [])];
import os; nid=os.environ.get("NF_ID","'"$NF_ID"'");
sys.exit(0 if nid not in ids else 1)' || die "rejected CHF still discoverable"
ok "not discoverable"

echo "== Valid CHF → 200/201 =="
code=$(curl_nrf -o /tmp/nrf-chf-ok.json -w '%{http_code}' \
  -X PUT "${NRF_URL}/nnrf-nfm/v1/nf-instances/${NF_ID}" \
  -H 'Content-Type: application/json' \
  --data-binary @"$VALID_FIXTURE")
[[ "$code" == "200" || "$code" == "201" ]] || die "valid want 201 got $code: $(cat /tmp/nrf-chf-ok.json)"
ok "valid CHF registered ($code)"

echo "== PATCH heartbeat =="
code=$(curl_nrf -o /tmp/nrf-chf-patch.json -w '%{http_code}' \
  -X PATCH "${NRF_URL}/nnrf-nfm/v1/nf-instances/${NF_ID}" \
  -H 'Content-Type: application/json-patch+json' \
  --data '[{"op":"replace","path":"/load","value":1}]')
[[ "$code" == "204" || "$code" == "200" ]] || die "PATCH want 204 got $code"
ok "PATCH ok"

echo "== URI/body ID mismatch → 400 =="
code=$(curl_nrf -o /tmp/nrf-chf-mismatch.json -w '%{http_code}' \
  -X PUT "${NRF_URL}/nnrf-nfm/v1/nf-instances/00000000-0000-0000-0000-000000000099" \
  -H 'Content-Type: application/json' \
  --data-binary @"$VALID_FIXTURE")
[[ "$code" == "400" ]] || die "mismatch want 400 got $code"
ok "URI/body mismatch → 400"

echo "ALL CHECKS PASSED"
