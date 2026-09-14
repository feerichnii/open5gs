#!/bin/sh
# Lab Root CA + NF certificates (E2-01)
set -e
dir="$(cd "$(dirname "$0")" && pwd)"
out="$dir/out"
mkdir -p "$out"
if [ ! -f "$out/ca.key" ]; then
  openssl req -x509 -newkey rsa:2048 -nodes \
    -keyout "$out/ca.key" -out "$out/ca.crt" -days 3650 \
    -subj "/CN=ca.5gc.lab"
fi
gen() {
  name=$1
  openssl req -newkey rsa:2048 -nodes \
    -keyout "$out/${name}.key" -out "$out/${name}.csr" \
    -subj "/CN=${name}01.5gc.lab"
  openssl x509 -req -in "$out/${name}.csr" \
    -CA "$out/ca.crt" -CAkey "$out/ca.key" -CAcreateserial \
    -out "$out/${name}.crt" -days 825 \
    -extfile <(printf "subjectAltName=DNS:%s01.5gc.lab" "$name")
}
for nf in nrf scp amf udm; do gen "$nf"; done
echo "PKI written to $out"
