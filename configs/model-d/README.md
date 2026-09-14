# Model D lab profile

Use with `configs/open5gs/*.yaml` via symlinks or `-c` overrides.

- SBI consumers: only `client.scp` (delegated discovery)
- TLS: enable in lab via `docker/lab/pki/generate.sh`
- OAuth2: enable `sbi.oauth2` **on NRF** (issuer + signing_key). Producers
  (UDM/AMF) may enable validation with the **same signing_key**. Do not
  enable producer OAuth without SCP Bearer injection (lab SCP mints tokens
  when `oauth2.enabled` is set on SCP with the shared key).

See `docs/lab/open5gs_model_d_unified_plan.md` §10 for review of commit `320bedb` fixes.
