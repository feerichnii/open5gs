# Model D lab profile

Use with `configs/open5gs/*.yaml` via symlinks or `-c` overrides.

- SBI consumers: only `client.scp` (delegated discovery)
- TLS: enable in lab via `docker/lab/pki/generate.sh`
- OAuth2: `sbi.oauth2.enabled: true` on NRF and producer NFs

See `docs/lab/open5gs_model_d_unified_plan.md`.
