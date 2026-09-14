# Upstream PR plan (Phase 7.4)

Split into reviewable PRs for `open5gs/open5gs`:

1. **NRF raw NFProfile storage** — `lib/sbi/nnrf-profile-raw.c`, discovery filters
2. **ProblemDetails `cause`** — `lib/sbi/ogs-sbi-cause.h`, NRF/SCP/AMF/UDM handlers
3. **AMF optional PCF** — `amf.pcf.mandatory` configuration
4. **OAuth2 lab profile** — optional compile-time or default-off NRF token endpoint

Each PR includes tests from `tests/nrf/` and CI job `lab-model-d.yml` adapted upstream.
