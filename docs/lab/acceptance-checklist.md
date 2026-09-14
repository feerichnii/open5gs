# Model D MVP acceptance (§8)

## After review fixes (§10.3 of unified plan)

- [x] E14 raw NFProfile from `request->http.content` + PATCH sync
- [x] E14 SUPI digit-range / regex match; preferred-locality as preference
- [x] E7-05 cause constants; 404/405/500 mapped more carefully in UDM/NRF
- [x] E15-01 optional PCF (`amf.pcf.mandatory: false`)
- [x] E5 OAuth2 form-urlencoded token + exp/aud check + SCP Bearer mint (shared key)
- [x] Build CI for NRF/SCP/AMF/UDM (`.github/workflows/lab-model-d.yml`)
- [ ] Full TLS/mTLS lab NF configs (E2)
- [ ] Async SCP→NRF AccessToken + CCA (E5-04/05 complete)
- [ ] SWAP-001..012 against vendor stacks
- [ ] E4 PCF decoupled from MongoDB

Update `docs/lab/open5gs_conformance_matrix.md` after each milestone.

