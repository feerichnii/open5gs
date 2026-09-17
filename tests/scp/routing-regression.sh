#!/usr/bin/env bash
# SCP routing regression checks (lab Model D / vendor CHF interop).
#
# Exercises live SCP when SCP_URL + NRF are up. Without SCP_URL, runs
# offline unit assertions via python3 (URI inference contract).
#
# Usage:
#   ./tests/scp/routing-regression.sh
#   SCP_URL=http://127.0.0.1:7777 NRF_HINT=http://172.16.7.100:18491 \
#     ./tests/scp/routing-regression.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SCP_URL="${SCP_URL:-}"
CONSUMER_UA="${CONSUMER_UA:-CHF-nexign-lab}"

die() { echo "FAIL: $*" >&2; exit 1; }
ok() { echo "OK: $*"; }

command -v python3 >/dev/null || die "python3 required"

echo "== Offline: URI → service contract =="
python3 - <<'PY'
# Mirror ogs_sbi_service_name_from_uri / status-notify detection at a
# string level (C unit tests cover the real helpers).
cases = [
    ("/nnrf-nfm/v1/nf-instances/x", "nnrf-nfm", False),
    ("/nnrf-nfm/v1/nf-status-notify", "nnrf-nfm", True),
    ("/nudm-sdm/v2/imsi-1/am-data", "nudm-sdm", False),
    ("/unknown-service/v1/x", None, False),
]
for uri, svc, notify in cases:
    parts = [p for p in uri.split("/") if p]
    got = parts[0] if parts else None
    known = {"nnrf-nfm","nudm-sdm","nsmf-pdusession","npcf-am-policy-control",
             "nchf-convergedcharging"}
    inferred = got if got in known else None
    assert inferred == svc, (uri, inferred, svc)
    is_notify = (got == "nnrf-nfm" and len(parts) >= 3 and parts[2] == "nf-status-notify")
    assert is_notify == notify, (uri, is_notify, notify)
print("offline contract ok")
PY
ok "offline URI contract"

if [[ -z "$SCP_URL" ]]; then
  ok "SCP_URL unset — skipping live HTTP checks"
  echo "ALL CHECKS PASSED (offline)"
  exit 0
fi

curl_scp() {
  curl --http2-prior-knowledge -sS "$@"
}

NF_ID="97986d02-4c4d-480d-b0e3-87de526bda20"
BODY=$(cat <<EOF
{"nfInstanceId":"${NF_ID}","nfType":"CHF","nfStatus":"REGISTERED","heartBeatTimer":10}
EOF
)

echo "== Test2: target=NRF, service-names missing, PUT nf-instances =="
code=$(curl_scp -o /tmp/scp-put.json -w '%{http_code}' \
  -X PUT "${SCP_URL}/nnrf-nfm/v1/nf-instances/${NF_ID}" \
  -H "User-Agent: ${CONSUMER_UA}" \
  -H "Content-Type: application/json" \
  -H "3gpp-Sbi-Discovery-target-nf-type: NRF" \
  --data-binary "$BODY")
# Expect forward success (200/201) or 503 if NRF down — never 500 with
# "No Mandatory Discovery" / "Invalid resource name"
body=$(cat /tmp/scp-put.json 2>/dev/null || true)
echo "HTTP $code body=${body:0:200}"
[[ "$code" != "500" ]] || die "unexpected 500: $body"
echo "$body" | grep -qi 'No Mandatory Discovery' && die "No Mandatory Discovery"
echo "$body" | grep -qi 'Invalid resource name' && die "Invalid resource name"
if [[ "$code" == "400" ]]; then
  echo "$body" | grep -qi 'Incomplete discovery' && die "incomplete discovery after inference"
fi
ok "PUT without service-names did not hit old reject paths (HTTP $code)"

echo "== Test7: URI nnrf-nfm vs service-names nudm-sdm → 400 =="
code=$(curl_scp -o /tmp/scp-mismatch.json -w '%{http_code}' \
  -X PUT "${SCP_URL}/nnrf-nfm/v1/nf-instances/${NF_ID}" \
  -H "User-Agent: ${CONSUMER_UA}" \
  -H "Content-Type: application/json" \
  -H "3gpp-Sbi-Discovery-target-nf-type: NRF" \
  -H "3gpp-Sbi-Discovery-service-names: nudm-sdm" \
  --data-binary "$BODY")
[[ "$code" == "400" ]] || die "mismatch want 400 got $code"
ok "service/URI mismatch → 400"

echo "== Test5: unknown URI service → 400 =="
code=$(curl_scp -o /tmp/scp-unknown.json -w '%{http_code}' \
  -X PUT "${SCP_URL}/unknown-service/v1/foo" \
  -H "User-Agent: ${CONSUMER_UA}" \
  -H "3gpp-Sbi-Discovery-target-nf-type: NRF")
[[ "$code" == "400" ]] || die "unknown want 400 got $code"
ok "unknown service → 400"

echo "== Test6: Target-apiRoot direct (if NRF_HINT set) =="
if [[ -n "${NRF_HINT:-}" ]]; then
  code=$(curl_scp -o /tmp/scp-tar.json -w '%{http_code}' \
    -X PATCH "${SCP_URL}/nnrf-nfm/v1/nf-instances/${NF_ID}" \
    -H "User-Agent: ${CONSUMER_UA}" \
    -H "Content-Type: application/json-patch+json" \
    -H "3gpp-Sbi-Target-apiRoot: ${NRF_HINT}" \
    --data '[{"op":"replace","path":"/load","value":0}]')
  [[ "$code" != "500" ]] || die "Target-apiRoot 500: $(cat /tmp/scp-tar.json)"
  ok "Target-apiRoot PATCH HTTP $code"
else
  ok "NRF_HINT unset — skip Target-apiRoot live test"
fi

echo "ALL CHECKS PASSED"
