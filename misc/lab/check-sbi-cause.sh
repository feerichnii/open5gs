#!/bin/sh
set -e
root="$(cd "$(dirname "$0")/../.." && pwd)"
fail=0
for dir in src/nrf src/scp src/amf src/udm; do
  matches="$(grep -rn 'ogs_sbi_server_send_error' "$root/$dir" --include='*.c' \
    | grep ', NULL)' || true)"
  if [ -n "$matches" ]; then
    echo "$matches"
    fail=1
  fi
done
if [ "$fail" -ne 0 ]; then
  echo "E7-05: ogs_sbi_server_send_error with NULL cause in lab NF code"
  exit 1
fi
echo "check-sbi-cause: ok"
