#!/usr/bin/env bash
# E14-08 / E5: live NRF HTTP/2 checks — register vendor PCF profile,
# discovery filters, PATCH, OAuth2 token + Bearer on protected APIs.
#
# Usage:
#   NRF_URL=http://127.0.0.1:7777 ./tests/nrf/raw-profile-discover.sh
#
# Open5GS nghttp2 server needs prior knowledge (no HTTP/1 Upgrade):
#   curl --http2-prior-knowledge
#
# Requires: curl with HTTP/2, python3.
set -euo pipefail

NRF_URL="${NRF_URL:-http://127.0.0.1:7777}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
FIXTURE="$ROOT/tests/nrf/fixtures/vendor-pcf-profile.json"
NF_ID="00000000-1111-2222-3333-444455556666"
CONSUMER_ID="amf-lab-1"
AUTH_HDR=()

die() { echo "FAIL: $*" >&2; exit 1; }
ok() { echo "OK: $*"; }

command -v python3 >/dev/null || die "python3 required"

curl_nrf() {
  # nghttp2 cleartext: prior-knowledge only (Upgrade → HTTP/0.9 garbage)
  curl --http2-prior-knowledge -sS "$@"
}

json_get() {
  python3 -c 'import json,sys; d=json.load(sys.stdin)
path=sys.argv[1].split(".")
cur=d
for p in path:
  if isinstance(cur,list): cur=cur[int(p)]
  else: cur=cur[p]
print(cur if not isinstance(cur,(dict,list)) else json.dumps(cur))' "$1"
}

json_has() {
  python3 -c 'import json,sys
d=json.load(sys.stdin)
path=sys.argv[1].split(".")
cur=d
try:
  for p in path:
    if isinstance(cur,list): cur=cur[int(p)]
    else: cur=cur[p]
  sys.exit(0 if cur not in (None,"") else 1)
except Exception:
  sys.exit(1)' "$1"
}

obtain_bearer() {
  local scope="$1"
  local http body token
  body=$(mktemp)
  http=$(curl_nrf -o "$body" -w '%{http_code}' \
    -X POST "${NRF_URL}/oauth2/token" \
    -H 'Content-Type: application/x-www-form-urlencoded' \
    --data "grant_type=client_credentials&nfInstanceId=${CONSUMER_ID}&nfType=AMF&targetNfType=NRF&scope=${scope}" || true)
  if [[ "$http" == "200" ]]; then
    token=$(json_get access_token <"$body")
    rm -f "$body"
    [[ -n "$token" ]] || die "empty access_token for scope=$scope"
    echo "$token"
    return 0
  fi
  rm -f "$body"
  if [[ "$http" == "404" ]]; then
    # oauth2 disabled on NRF
    echo ""
    return 0
  fi
  die "token HTTP $http for scope=$scope"
}

echo "== OAuth2 bootstrap =="
# nnrf-nfm is typically oauth-exempt; nnrf-disc is not when oauth2.enabled
TOK_DISC=$(obtain_bearer "nnrf-disc")
TOK_NFM=$(obtain_bearer "nnrf-nfm")
if [[ -n "$TOK_DISC" ]]; then
  AUTH_HDR=(-H "Authorization: Bearer ${TOK_DISC}")
  ok "Bearer acquired (oauth2 enabled)"
else
  AUTH_HDR=()
  ok "oauth2 disabled — continuing without Bearer"
fi

echo "== PUT NFProfile =="
# NFM often exempt; still send Bearer if we have one
code=$(curl_nrf -o /tmp/nrf-put.json -w '%{http_code}' \
  -X PUT "${NRF_URL}/nnrf-nfm/v1/nf-instances/${NF_ID}" \
  -H 'Content-Type: application/json' \
  ${TOK_NFM:+-H "Authorization: Bearer ${TOK_NFM}"} \
  --data-binary @"$FIXTURE")
[[ "$code" == "200" || "$code" == "201" ]] || die "PUT got HTTP $code $(cat /tmp/nrf-put.json)"
ok "registered $NF_ID ($code)"

echo "== Discovery preserve vendor fields =="
disc=$(curl_nrf "${AUTH_HDR[@]}" \
  "${NRF_URL}/nnrf-disc/v1/nf-instances?target-nf-type=PCF&requester-nf-type=AMF")
echo "$disc" | json_has "nfInstances.0.pcfInfo.supiRanges" || die "pcfInfo.supiRanges missing: $disc"
echo "$disc" | json_has "nfInstances.0.nfSetIdList" || die "nfSetIdList missing"
echo "$disc" | json_has "nfInstances.0.locality" || die "locality missing"
echo "$disc" | json_has "nfInstances.0.nfServices.0.apiPrefix" || die "apiPrefix missing"
ok "E14-01 vendor fields preserved"

echo "== SUPI / nf-set-id filters =="
in_range=$(curl_nrf "${AUTH_HDR[@]}" \
  "${NRF_URL}/nnrf-disc/v1/nf-instances?target-nf-type=PCF&requester-nf-type=AMF&supi=imsi-001010000000050")
out_range=$(curl_nrf "${AUTH_HDR[@]}" \
  "${NRF_URL}/nnrf-disc/v1/nf-instances?target-nf-type=PCF&requester-nf-type=AMF&supi=imsi-001010000000200")
set_ok=$(curl_nrf "${AUTH_HDR[@]}" \
  "${NRF_URL}/nnrf-disc/v1/nf-instances?target-nf-type=PCF&requester-nf-type=AMF&nf-set-id=set1.pcf.5gc.mnc001.mcc001.3gppnetwork.org")
set_bad=$(curl_nrf "${AUTH_HDR[@]}" \
  "${NRF_URL}/nnrf-disc/v1/nf-instances?target-nf-type=PCF&requester-nf-type=AMF&nf-set-id=wrong.set")

python3 - <<PY
import json
def n(s):
  d=json.loads(s); return len(d.get("nfInstances") or [])
assert n('''$in_range''')==1, "in-range SUPI"
assert n('''$out_range''')==0, "out-of-range SUPI"
assert n('''$set_ok''')==1, "nf-set-id match"
assert n('''$set_bad''')==0, "nf-set-id miss"
print("filters ok")
PY
ok "E14-03 filters"

echo "== PATCH replace /load /priority + add /locality =="
patch_code=$(curl_nrf -o /tmp/nrf-patch.json -w '%{http_code}' \
  -X PATCH "${NRF_URL}/nnrf-nfm/v1/nf-instances/${NF_ID}" \
  -H 'Content-Type: application/json-patch+json' \
  ${TOK_NFM:+-H "Authorization: Bearer ${TOK_NFM}"} \
  --data '[{"op":"replace","path":"/load","value":77},{"op":"replace","path":"/priority","value":3},{"op":"add","path":"/locality","value":"area2"}]')
[[ "$patch_code" == "204" || "$patch_code" == "200" ]] || die "PATCH HTTP $patch_code $(cat /tmp/nrf-patch.json)"

get=$(curl_nrf ${TOK_NFM:+-H "Authorization: Bearer ${TOK_NFM}"} \
  "${NRF_URL}/nnrf-nfm/v1/nf-instances/${NF_ID}")
load=$(echo "$get" | json_get load)
prio=$(echo "$get" | json_get priority)
loc=$(echo "$get" | json_get locality)
[[ "$load" == "77" ]] || die "load=$load want 77"
[[ "$prio" == "3" ]] || die "priority=$prio want 3"
[[ "$loc" == "area2" ]] || die "locality=$loc want area2"
ok "PATCH applied to profile"

echo "== OAuth2 /oauth2/token negative =="
tok_http=$(curl_nrf -o /tmp/nrf-tok.json -w '%{http_code}' \
  -X POST "${NRF_URL}/oauth2/token" \
  -H 'Content-Type: application/x-www-form-urlencoded' \
  --data 'grant_type=client_credentials&nfInstanceId=amf-lab-1&nfType=AMF&targetNfType=UDM&scope=nudm-sdm')
[[ "$tok_http" != "400" ]] || die "oauth still 400: $(cat /tmp/nrf-tok.json)"
if [[ "$tok_http" == "200" ]]; then
  cat /tmp/nrf-tok.json | json_has access_token || die "no access_token"
  bad=$(curl_nrf -o /tmp/nrf-tok-bad.json -w '%{http_code}' \
    -X POST "${NRF_URL}/oauth2/token" \
    -H 'Content-Type: application/x-www-form-urlencoded' \
    --data 'grant_type=password&nfInstanceId=x&targetNfType=UDM')
  [[ "$bad" == "400" ]] || die "password grant want 400 got $bad"
  get405=$(curl_nrf -o /dev/null -w '%{http_code}' \
    -X GET "${NRF_URL}/oauth2/token")
  [[ "$get405" == "405" ]] || die "GET token want 405 got $get405"
  ok "E5-02 token + negatives"
else
  ok "OAuth2 endpoint reachable (HTTP $tok_http)"
fi

echo "== 404 cause NF_INSTANCE_NOT_FOUND =="
nf404=$(curl_nrf -o /tmp/nrf-404.json -w '%{http_code}' \
  ${TOK_NFM:+-H "Authorization: Bearer ${TOK_NFM}"} \
  "${NRF_URL}/nnrf-nfm/v1/nf-instances/00000000-0000-0000-0000-000000000000")
[[ "$nf404" == "404" ]] || die "expected 404 got $nf404"
python3 - <<'PY'
import json
d=json.load(open("/tmp/nrf-404.json"))
cause=d.get("cause") or (d.get("problemDetails") or {}).get("cause")
assert cause=="NF_INSTANCE_NOT_FOUND", cause
print("cause ok")
PY
ok "NRF 404 cause"

echo "ALL CHECKS PASSED"
