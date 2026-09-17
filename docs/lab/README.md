# Open5GS Model D — changelog наших изменений

Форк [`feerichnii/open5gs`](https://github.com/feerichnii/open5gs) для лаборатории **5GC Communication Model D** (TS 23.501 Annex E).

**Профиль топологии:** Open5GS = **NRF + SCP + AMF + UDM**; остальные NF — вендорские (SMF, PCF, CHF, UDR, …).

**База:** upstream Open5GS ≈ `2d3bb46` (v2.8.x).  
**Ветки:** `main`, `lab/model-d` (синхронизируются).  
**Актуальный HEAD лабораторных коммитов:** `af5f47d` (2026-09-14).

| Документ | Назначение |
|---|---|
| [open5gs_model_d_unified_plan.md](open5gs_model_d_unified_plan.md) | Полный план + ревью коммитов (§10) |
| [open5gs_conformance_matrix.md](open5gs_conformance_matrix.md) | Матрица соответствия 3GPP |
| [acceptance-checklist.md](acceptance-checklist.md) | Чек-лист приёмки MVP |
| [upstream-pr-plan.md](upstream-pr-plan.md) | Набросок PR в upstream |
| [open5gs_scp_3gpp_interop_fix.md](open5gs_scp_3gpp_interop_fix.md) | SCP routing fix (vendor CHF / Nnrf_NFManagement) |
| [open5gs_nrf_3gpp_compliance_hardening.md](open5gs_nrf_3gpp_compliance_hardening.md) | NRF NFRegister: strict NFProfile validation (400 vs crash) |
| [../error-matrix.md](../error-matrix.md) | Маппинг HTTP status → `cause` |

---

## 1. Зачем

Сделать так, чтобы вендорские NF могли:

1. регистрироваться в Open5GS NRF **без потери** vendor-полей профиля (`*Info`, `nfSetIdList`, `locality`, …);
2. находиться через discovery с фильтрами `supi` / `routing-indicator` / `nf-set-id`;
3. ходить через SCP (Model D) с Bearer-токенами;
4. получать осмысленные `ProblemDetails.cause`, а не `NULL` / один cause на всё;
5. регистрировать UE в AMF **без обязательного PCF** (если вендорский PCF ещё не подключён).

---

## 2. Коммиты (хронология)

| Commit | Суть |
|---|---|
| `320bedb` | Первый слой: raw NFProfile, OAuth stub, cause, optional PCF, lab docs/CI |
| `a8eb926` | §10.3: сборка, form OAuth, SUPI digits/regex, SCP Bearer mint, CI meson |
| `f65fd40` | §10.4: oauth до parse, PATCH→struct+raw, NRF 404 causes, scope check |
| `af5f47d` | §10.5: probe-скрипт h2+Bearer, UDM Invalid* causes, PATCH cause |
| `cdf753b` | docs/lab README changelog |
| *(SCP routing)* | URI service inference; transit `nnrf-nfm` → configured NRF; local SM only `nf-status-notify` |
| *(NRF hardening)* | Strict NFProfile validation before register; safe client associate; rollback on failure |

Дифф относительно upstream-базы: **~56 файлов, +3200 / −250**.

---

## 3. Что сделано по EPIC

### E14 — NRF: сохранность профилей и discovery

| ID | Что |
|---|---|
| **E14-01** | Сырой `NFProfile` как `cJSON *raw_profile` в `ogs_sbi_nf_instance_t`. При PUT/register тело берётся из `request->http.content`. Discovery/GET отдают parse из raw + runtime (`load`/`priority`/`capacity`/`nfStatus`). |
| **E14-01 PATCH** | `replace` / `add` / `remove` применяются к raw и к полям instance; неподдерживаемый `op` → 400 `MANDATORY_IE_INCORRECT`. |
| **E14-03** | Фильтры: `supi` (digit-range без `imsi-` + ERE `pattern`), `routing-indicator`, `nf-set-id`. `preferred-locality` — предпочтение (score), не жёсткий отсев. NF без диапазонов **не** отфильтровывается. |
| **E14-04** | SCP/AMF пробрасывают `3gpp-Sbi-Discovery-supi/-routing-indicator/-nf-set-id/-preferred-locality`; AMF кладёт routing indicator / SUPI при discovery AUSF/UDM. |
| **E14-08** | Fixture `tests/nrf/fixtures/vendor-pcf-profile.json` + живой прогон `tests/nrf/raw-profile-discover.sh`. |
| **E14-hardening** | Strict TS 29.510 checks before import: NF-level addressing, map key == `serviceInstanceId`, unique ids, non-empty `apiFullVersion`. Malformed CHF → **400** ProblemDetails; associate failures roll back instance (not discoverable). |

**Код:** `lib/sbi/nnrf-profile-raw.c/.h`, `lib/sbi/nnrf-profile-validate.c/.h`, `src/nrf/nnrf-handler.c`, `src/nrf/nrf-sm.c`, `lib/sbi/message.c`, `lib/sbi/context.c`.

### E5 — OAuth2 (Nnrf_AccessToken, lab HS256)

| ID | Что |
|---|---|
| **E5-02** | `POST /oauth2/token` **до** `ogs_sbi_parse_request()` (form-urlencoded не ломает JSON-парсер). Парсинг `grant_type`, `nfInstanceId`, `nfType`, `scope`, `targetNfType`, …. Ответ JSON: token / Bearer / expires_in / scope. JWT: `sub`=requester, `aud`=targetNfType. |
| **E5-03** | Middleware на продюсере: Bearer, подпись HS256, `exp`, `aud`, `scope` (имя сервиса из URI ⊆ scope). Exempt: `/nnrf-nfm/`, `/oauth2/`. |
| **E5-05** | SCP при исходящем запросе к producer мint’ит Bearer общим ключом; `sub` = `3gpp-Sbi-Discovery-requester-nf-instance-id` (иначе SCP id). |

**Код:** `lib/sbi/oauth.c/.h`, `lib/sbi/jwt.c/.h`, `src/nrf/nnrf-handler.c`, `src/scp/sbi-path.c`, `lib/sbi/nghttp2-server.c`.

> **Ограничение:** SCP пока **не** ходит в NRF за токеном по HTTP (нужен async state machine). Shared-key mint — lab-временное решение. Direct-mode Bearer в AMF/UDM без SCP — нет. CCA (E5-04) — нет.

### E7-05 — ProblemDetails `cause`

- Константы: `lib/sbi/ogs-sbi-cause.h` (`NF_INSTANCE_NOT_FOUND`, `SUBSCRIPTION_NOT_FOUND`, `USER_NOT_FOUND`, …).
- Убраны `NULL` cause в вызовах `ogs_sbi_server_send_error` для NRF/SCP/AMF/UDM.
- NRF 404: instance → `NF_INSTANCE_NOT_FOUND`, subscription → `SUBSCRIPTION_NOT_FOUND`.
- UDM: Invalid HTTP method → 405 + `INVALID_MSG_FORMAT`; Invalid API/resource → `UNSUPPORTED_RESOURCE_URI`; Invalid RAND → `MANDATORY_IE_INCORRECT`. Оставшиеся `MANDATORY_IE_MISSING` в основном на 400 «No …» (по TS это корректно).
- CI-скрипт: `misc/lab/check-sbi-cause.sh` (отсутствие `NULL`).

### E15-01 — AMF без обязательного PCF

- Конфиг: `amf.pcf.mandatory` (default `true`).
- При `false` регистрация продолжается без AM Policy (`pcf_bypass_registration_accept`).

**Код:** `src/amf/context.c/.h`, `src/amf/gmm-sm.c`.

### E3-09 — loop protection (частично)

- SCP считает заголовки `Via` (>8 → ошибка). Сам Via пока не добавляет стабильно → между двумя Open5GS SCP счётчик может не расти.

### Lab / CI / docs

- Docker/DNS/PKI заготовки: `docker/lab/` (`docker-compose.yml`, CoreDNS, `pki/generate.sh`).
- Заметки профиля: `configs/model-d/README.md`.
- CI: `.github/workflows/lab-model-d.yml` — cause audit + сборка `nrf`/`scp`/`amf`/`udm`.
- Док-пакет: `docs/lab/*`, `docs/error-matrix.md`, `docs/interface-inventory.md`, `docs/standards-baseline.md`.

---

## 4. Карта ключевых файлов

```
lib/sbi/
  nnrf-profile-raw.c/.h        # raw NFProfile + filters
  nnrf-profile-validate.c/.h   # strict NFProfile semantic checks
  oauth.c/.h, jwt.c/.h         # token issue / verify
  ogs-sbi-cause.h              # cause constants
src/nrf/
  nrf-sm.c                # /oauth2/token до parse
  nnrf-handler.c          # register/PATCH/token/causes + validate/rollback
src/scp/sbi-path.c        # Discovery-*, URI inference, NRF transit, Bearer mint
src/scp/scp-sm.c          # local nf-status-notify only
src/amf/                  # optional PCF, discovery params
src/udm/                  # cause mapping
tests/nrf/
  fixtures/vendor-pcf-profile.json
  fixtures/chf-nexign-crash-profile.json
  fixtures/chf-valid-profile.json
  raw-profile-discover.sh   # live HTTP/2 probe
  nfprofile-validate.sh     # malformed vs valid CHF register
tests/scp/routing-regression.sh
.github/workflows/lab-model-d.yml
```

---

## 5. Как проверить

### Сборка (Ubuntu)

```bash
meson setup build && ninja -C build \
  src/nrf/open5gs-nrfd \
  src/scp/open5gs-scpd \
  src/amf/open5gs-amfd \
  src/udm/open5gs-udmd
```

### Живой прогон NRF

Включить на NRF `sbi.oauth2.enabled: true` (+ `issuer`, `signing_key`). Затем:

```bash
NRF_URL=http://127.0.0.1:7777 ./tests/nrf/raw-profile-discover.sh
```

Скрипт использует **`--http2-prior-knowledge`**, получает Bearer через `/oauth2/token` и проверяет PUT → discovery/filters → PATCH → token negatives → 404 cause.

### SCP routing (vendor CHF / Nnrf_NFManagement)

```bash
./tests/scp/routing-regression.sh
# live:
SCP_URL=http://scp:7777 NRF_HINT=http://172.16.7.100:18491 \
  ./tests/scp/routing-regression.sh
```

Спека: [open5gs_scp_3gpp_interop_fix.md](open5gs_scp_3gpp_interop_fix.md).

### Fixture без NRF

```bash
python3 tests/nrf/test_e14_08_fixture.py
./tests/nrf/nfprofile-validate.sh          # offline fixture contract
# live (NRF up, OAuth off or exempt for /nnrf-nfm):
NRF_URL=http://127.0.0.1:7777 ./tests/nrf/nfprofile-validate.sh
```

Спека: [open5gs_nrf_3gpp_compliance_hardening.md](open5gs_nrf_3gpp_compliance_hardening.md).

---

## 6. Что ещё не сделано (открытый хвост §9.1 / §10.5)

| Тема | Статус |
|---|---|
| E2: https/mTLS в конфигах NF (не только PKI-скрипт) | ⏳ |
| E5-04 CCA | ❌ |
| E5-05 полный async SCP → `Nnrf_AccessToken` | ⏳ (есть shared-key mint) |
| AMF/UDM Bearer в direct mode (без SCP) | ❌ |
| E16-07 UDM↔vendor UDR e2e | заглушка |
| E0-04 валидатор pcap/SBI | ❌ |
| SWAP-001..012 против вендорских стеков | ❌ |
| E4/E8/E9/E10/E11 (вне профиля 4 NF) | вне scope MVP |

Детали и история ревью — в плане §10.1–§10.6.

---

## 7. Конфиг OAuth (кратко)

1. **NRF:** `sbi.oauth2.enabled: true`, общий `signing_key`, `issuer`.
2. **Продюсеры (UDM/AMF):** тот же `signing_key`, `enabled: true` — только если клиенты шлют Bearer (через SCP mint или свой токен).
3. **SCP:** `oauth2.enabled: true` + тот же ключ → mint при forwarding.

Пока async AccessToken нет — **не** включать oauth на продюсерах без SCP/shared-key схемы.

---

## 8. Лицензия / upstream

Изменения поверх Open5GS (AGPL-3.0). Кандидаты на аккуратный upstream PR — см. [upstream-pr-plan.md](upstream-pr-plan.md) (raw profile / cause / optional PCF лучше дробить).
