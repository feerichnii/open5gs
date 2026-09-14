# Interface inventory (Model D lab)

Detailed matrix: `docs/lab/open5gs_conformance_matrix.md`.

## Lab topology (primary)

| Component | Role |
|---|---|
| Open5GS NRF | Nnrf_NFM, Nnrf_Disc, Nnrf_AccessToken (lab) |
| Open5GS SCP | Model D delegated discovery |
| Open5GS AMF | N2/N1, SBI consumer |
| Open5GS UDM | Nudm_*, Nudr client to vendor UDR |
| Vendor | SMF, PCF, CHF, UPF, AUSF, UDR, NSSF, BSF |

## Transport

- SBI: HTTP/2, TLS 1.2+ (lab profile `configs/model-d/`)
- Discovery: SCP + `3gpp-Sbi-Discovery-*` only (no producer URI in consumer config)
