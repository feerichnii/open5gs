# Open5GS → стандартная 5GC-лаборатория (Model D)
## Объединённый план разработки

**Версия:** 1.1, 2026-09-14 (v1.0 — 2026-09-11)
**Источники:** анализ кода `feerichnii/open5gs` (зеркало `open5gs/open5gs` main, v2.8.0, HEAD `2d3bb46`), план разработки по фазам (Phase 0–7), документ `open5gs_model_d_lab_tasks.md` (LAB-xxx / SWAP-xxx), матрица соответствия `open5gs_conformance_matrix.md` (v1.1).

> **Изменения v1.1:** добавлены EPIC E14 (NRF: сохранность профилей и discovery), E15 (AMF-пробелы), E16 (UDM-пробелы), задача E7-05 (`cause` в ProblemDetails), раздел 9 — профиль топологии «Open5GS = NRF+SCP+AMF+UDM, остальное вендорское» с пересобранными приоритетами. Статусы в §3.2 уточнены по матрице.

---

## 0. Как читать документ

Каждая задача имеет:

| Поле | Значения |
|---|---|
| **Тип** | `CFG` — конфигурация/деплой без изменения кода · `CODE` — изменение исходников Open5GS · `NEW` — новый компонент/сервис · `TEST` — тест или валидатор · `DOC` — документация |
| **Приоритет** | `P0` — без этого лаборатория не считается стандартной · `P1` — нужно для полноты Model D · `P2` — расширение |
| **Оценка** | человеко-дни, ориентировочно, для разработчика, знакомого с кодовой базой Open5GS |
| **Источник** | `Phase X.Y` — исходный план по фазам; `LAB-nnn` / `SWAP-nnn` — документ ChatGPT; `NEW` — задача, отсутствовавшая в обоих |
| **Статус в коде** | ✅ реализовано в 2.8.0 · ⚠️ частично · ❌ отсутствует |

Идентификаторы задач: `E<epic>-<nn>`. Перекрёстная таблица LAB-xxx → E-xx в Приложении A.

---

## 1. Цель и критерий приёмки

Преобразовать Open5GS в лабораторию 5GC, в которой:

1. каждая 3GPP Network Function — отдельный процесс/контейнер с собственным IP, FQDN, сертификатом и конфигурацией;
2. меж-NF взаимодействие идёт только по интерфейсам, определённым 3GPP (SBI по TS 29.5xx, N2 по TS 38.413, N4 по TS 29.244, N3 по TS 29.281);
3. SBA работает в Communication Model D (TS 23.501 Annex E): консьюмер знает только SCP, discovery делегирован SCP → NRF;
4. SBI защищён по TS 33.501 §13: TLS 1.2/1.3, mTLS, OAuth2 (Nnrf_AccessToken) с CCA;
5. NF заменяема сторонней реализацией без изменения кода соседей.

**Главный критерий приёмки (из LAB-документа, принимается без изменений):**

> Любая NF должна быть заменяема другой реализацией при сохранении стандартного 3GPP-интерфейса и без изменения кода соседних NF. Работа `Open5GS NF-A ↔ Open5GS NF-B` не является доказательством стандартности; доказательством является `Open5GS NF-A ↔ External NF-B` и обратно.

---

## 2. Архитектурные правила

| ID | Правило | Комментарий по коду |
|---|---|---|
| RULE-001 | Только стандартные inter-NF интерфейсы: никакой БД, ФС, C-структур, private API, shared state чужой NF | Нарушитель в 5GC один — PCF (см. E4) |
| RULE-002 | Замена NF допускает изменение только bootstrap-конфига, DNS, сертификатов, NRF-профиля | — |
| RULE-003 | Model D: консьюмер не знает адрес продюсера; discovery через SCP; прямые URI продюсеров в конфиге запрещены | Уже соблюдается в default-конфигах: только `client.scp`. Исключение — `nrfId` в NSSF (стандартно, TS 29.531) |
| RULE-004 | У persistence один владелец (UDR → MongoDB). HSS/PCRF — владельцы своих данных в EPC-профиле | `libogsdbi` линкуют HSS, PCRF, UDR, PCF |
| RULE-005 | Тесты привязаны к версиям 3GPP Release / TS / OpenAPI / RFC | — |
| RULE-006 *(NEW)* | Каждая SBI-транзакция несёт binding: продюсер отдаёт `3gpp-Sbi-Binding`, консьюмер возвращает `3gpp-Sbi-Routing-Binding`; SCP маршрутизирует по binding, а не по кэшу URI | Заголовков нет в коде (E6) |
| RULE-007 *(NEW)* | Каждая задача типа CODE указывает файлы-точки входа и сопровождается тестом в `tests/` и pcap в `tests/pcap/` | — |

---

## 3. Baseline стандартов

### 3.1 Release

**Решение:** baseline = **3GPP Release 19 для схем OpenAPI**, процедуры — фактически Rel-16/17. Обоснование: модели в `lib/sbi/openapi/model` (3550 файлов) сгенерированы из `r19-20260402-openapitools-7.20.0`; откат на Rel-18 (как предлагал LAB-001) означает перегенерацию без функциональной выгоды. Фиксируется в `docs/standards-baseline.md`.

### 3.2 Матрица интерфейсов (Standards Matrix + фактическое состояние)

| Consumer → Producer | Service / Interface | TS | Статус в 2.8.0 | Примечание |
|---|---|---|---|---|
| gNB ↔ AMF | N2 / NGAP over SCTP | 38.413, RFC 9260 | ✅ | `src/amf/ngap-*.c` |
| UE ↔ AMF | N1 / NAS 5GMM/5GSM | 24.501 | ✅ | `lib/nas/5gs` |
| AMF → AUSF | Nausf_UEAuthentication | 29.509 | ✅ | через SCP |
| AUSF → UDM | Nudm_UEAuthentication | 29.503 | ✅ | |
| AMF → UDM | Nudm_UECM, Nudm_SDM | 29.503 | ✅ | |
| UDM → UDR | Nudr_DataRepository (subscription-data) | 29.504/29.505 | ✅ | |
| AMF → NSSF | Nnssf_NSSelection | 29.531 | ✅ | |
| AMF → PCF | Npcf_AMPolicyControl | 29.507 | ✅ | |
| AMF → SMF | Nsmf_PDUSession | 29.502 | ✅ | |
| SMF → PCF | Npcf_SMPolicyControl | 29.512 | ✅ | |
| PCF → UDR | Nudr_DataRepository (policy-data) | 29.504/29.519 | ⚠️ | Nudr есть, но параллельно прямой `ogs_dbi_*` в MongoDB |
| PCF → BSF | Nbsf_Management | 29.521 | ✅ | |
| SMF → UPF | N4 / PFCP | 29.244 | ✅ | |
| gNB ↔ UPF | N3 / GTP-U | 29.281 | ✅ | |
| SMF → CHF | Nchf_ConvergedCharging | 32.291 | ❌ | SMF использует Diameter Gy (`gy-path.c`) |
| NF → NRF | Nnrf_NFManagement | 29.510 | ⚠️ | профиль сохраняется частично: теряются `udrInfo/udmInfo/ausfInfo/pcfInfo/chfInfo/bsfInfo/nssfInfo/upfInfo`, `nfSetIdList`, `supportedFeatures` (см. матрицу §1.2) |
| SCP → NRF | Nnrf_NFDiscovery (delegated) | 29.510, 29.500 §6.10 | ⚠️ | `3gpp-Sbi-Discovery-*` ✅; фильтры `supi`, `routing-indicator`, `nf-set-id` не применяются |
| NF/SCP → NRF | Nnrf_AccessToken (OAuth2) | 29.510 §5.4, 33.501 §13.4 | ❌ | только сгенерированные модели |
| Все SBI | Binding / Routing-Binding | 29.500 §6.12 | ❌ | |
| Все SBI | Oci / Lci (overload/load control) | 29.500 §6.3, §6.4 | ❌ | |
| SEPP ↔ SEPP | N32-c, N32-f (TLS) | 29.573 | ✅ | `src/sepp`, PRINS ❌ |
| SMF/PGW-C ↔ SGW-C | S5/S8 GTP-C | 29.274 | ✅ | EPC-профиль |
| MME → HSS, PCRF | S6a, Gx (Diameter) | 29.272, 29.212, RFC 6733 | ✅ | EPC-профиль |

### 3.3 RFC Matrix

| Область | RFC | Статус |
|---|---|---|
| HTTP semantics | 9110 | ✅ |
| HTTP/2 | 9113 (nghttp2 server, curl client) | ✅ |
| TLS | 8446 (1.3), 5246 (1.2) | ✅ опционально, выключен по умолчанию |
| X.509 / hostname | 5280, 6125 | ✅ через OpenSSL |
| OAuth2 / JWT | 6749, 7519, 7515 | ❌ |
| JSON / Problem Details | 8259, 9457 (ex-7807) | ⚠️ структура ✅, `cause` в 6% ошибок |
| SCTP | 9260 | ✅ |
| URI | 3986 | ✅ |
| Diameter | 6733 | ✅ (EPC) |

### 3.4 Обязательный набор NF

**MVP (P0):** NRF, SCP, AMF, SMF, UPF, AUSF, UDM, UDR, PCF, NSSF, BSF + gNB/UE-симулятор, DNS, PKI.
**Расширения:** CHF (P1), SEPP + второй PLMN (P2, код есть), NEF/NWDAF/SMSF/AF/LMF/UDSF (P2, стабы).

---

## 4. Целевая архитектура

```text
                       ┌──────────┐
                       │   NRF    │◄── Nnrf_NFManagement (регистрация всех NF)
                       │ + OAuth2 │◄── Nnrf_AccessToken (CCA от SCP)
                       └────▲─────┘
                            │ Nnrf_NFDiscovery
                       ┌────┴─────┐
   Consumer ──────────►│   SCP    │──────────► Producer
   (знает только SCP)  │ Model D  │  routing по Discovery-*/Routing-Binding
                       └──────────┘  + Bearer token от имени консьюмера

   AMF · SMF · AUSF · UDM · UDR · PCF · NSSF · BSF · CHF*   — все за SCP
   UDR ── MongoDB (единственный владелец БД в 5GC)

   gNB ──N2/NGAP── AMF        gNB ──N3/GTP-U── UPF ──N6── DN
   UE  ──N1/NAS─── AMF        SMF ──N4/PFCP─── UPF

   PLMN-A: SCP ── SEPP-A ══N32══ SEPP-B ── SCP : PLMN-B   (P2)
```

Сетевой план: каждая NF — контейнер/netns, `<nf>01.5gc.lab`, `sbi.server: dev:eth0 + advertise: <fqdn>`, `https://` везде, CA `ca.5gc.lab`.

---

## 5. EPIC-и и задачи

### E0 — Baseline, инструменты, CI

| ID | Задача | Тип | Prio | Дни | Источник |
|---|---|---|---|---|---|
| E0-01 | Ветка `lab/model-d` от `2d3bb46`; CI: meson build, `tests/`, clang-tidy, сборка Docker-образов по NF | CFG | P0 | 2 | Phase 0.1 |
| E0-02 | `docs/standards-baseline.md`: Release, версии TS, версии OpenAPI (`lib/sbi/openapi/support/r19-*`), RFC-матрица (§3) | DOC | P0 | 1 | LAB-001..003 |
| E0-03 | `docs/interface-inventory.md`: для каждой связи consumer/producer/service/transport/security/discovery/ошибки; статус ✅⚠️❌ | DOC | P0 | 2 | LAB-002, 005 |
| E0-04 | Валидатор SBI: прокси перед SCP (или tshark-постобработка), проверка запросов/ответов и ProblemDetails против OpenAPI r19; отчёт нарушений как артефакт CI | NEW/TEST | P0 | 5 | Phase 0.4, LAB-130/131, LAB-101 |
| E0-05 | Трассировка: профили tshark (NGAP, PFCP, HTTP/2 + `SSLKEYLOGFILE`), скрипт сохранения pcap + SBI JSON + NRF-профилей + решений SCP на каждый интеграционный тест | TEST | P0 | 3 | Phase 0.5, LAB-136, LAB-144 |
| E0-06 | Выбор и интеграция RAN/UE-симулятора (UERANSIM или PacketRusher) в compose; эталонный сценарий Registration → PDU Session → Deregistration | CFG/TEST | P0 | 2 | Phase 0.5 |
| E0-07 | Структура `tests/{sbi,nrf,scp,oauth,pfcp,gtpu,ngap,nas,charging,interop,negative}` и раннер | TEST | P0 | 2 | LAB EPIC 12 |

**DoD E0:** зелёный CI на эталонном сценарии; в артефактах pcap и отчёт валидатора без ошибок схем.

---

### E1 — Изоляция NF, сеть, DNS

| ID | Задача | Тип | Prio | Дни | Источник |
|---|---|---|---|---|---|
| E1-01 | Docker Compose / netns: по контейнеру на NRF, SCP, AMF, SMF, UPF, AUSF, UDM, UDR, PCF, NSSF, BSF, MongoDB (только для UDR), DNS, CA | CFG | P0 | 3 | Phase 0.2, LAB-010 |
| E1-02 | IP-план `10.5.0.0/24` (SBI), `10.5.1.0/24` (N2/N3), `10.5.2.0/24` (N4), N6 — отдельный bridge; ни одного `127.0.0.0/8` между NF | CFG | P0 | 1 | LAB-011 |
| E1-03 | DNS (CoreDNS): A/AAAA для `<nf>01.5gc.lab`; конфиги NF используют FQDN + `advertise:` | CFG | P0 | 1 | LAB-020..022 |
| E1-04 | Конфиг каждой NF как отдельный файл + env-overrides (`OGS_*`), без ссылок на внутреннее состояние соседей | CFG | P0 | 1 | LAB-012 |
| E1-05 | Аудит shared FS/DB: зафиксировать результат — `libogsdbi` в HSS, PCRF, UDR, PCF; общий `tls/` допустим только как lab-инфраструктура | DOC | P0 | 0.5 | LAB-013..015 |
| E1-06 | Проверка UDM→UDR boundary (Nudr, без прямой БД) — тест SWAP-005 частично | TEST | P0 | 1 | LAB-017 |
| E1-07 | Dual-stack IPv6 на SBI/N2/N3 | CFG/TEST | P2 | 2 | LAB-022 |
| ~~—~~ | ~~SRV/NAPTR~~ — не применимо к 5GC SBA (discovery через NRF); NAPTR (TS 29.303) только для EPC-профиля, Open5GS его не реализует | — | — | — | LAB-023..025 → отклонено |
| ~~—~~ | ~~mixed-version NF~~ — вытекает из swap-тестов; отдельной задачи не требует | — | — | — | LAB-018 → объединено с E13 |

**DoD E1:** `docker compose down <nf>` останавливает одну NF, остальные живут; эталонный сценарий проходит без loopback-адресов.

---

### E2 — PKI, TLS, HTTP/2

| ID | Задача | Тип | Prio | Дни | Источник |
|---|---|---|---|---|---|
| E2-01 | Lab Root CA + intermediate; скрипт выпуска/перевыпуска; ключ CA вне compose | CFG | P0 | 1 | Phase 0.3, LAB-030 |
| E2-02 | Индивидуальный key pair + cert на NF, SAN = `DNS:<nf>01.5gc.lab`; хранение в `configs/tls/<nf>/` | CFG | P0 | 1 | LAB-031, 032 |
| E2-03 | `default.tls.server.scheme: https` + `client.cacert` во всех NF, SCP, NRF; `verify_client: true` (mTLS) | CFG | P0 | 1 | Phase 1.3, LAB-033, 034 |
| E2-04 | Проверить/включить hostname verification в `lib/sbi/client.c` (curl `CURLOPT_SSL_VERIFYHOST`) и ALPN `h2`; без fallback на HTTP/1.1 | CODE (проверка) | P0 | 1 | LAB-035, 036 |
| E2-05 | TLS negative tests: expired, not-yet-valid, unknown CA, чужой SAN, IP вместо FQDN, TLS 1.1 | TEST | P1 | 2 | LAB-037 |
| E2-06 | TLS observability: peer, версия, cipher, subject/SAN, причина reject в логах `lib/sbi` | CODE | P1 | 1 | LAB-038 |

**DoD E2:** в pcap эталонного сценария нет ни одного plaintext HTTP; сертификат чужой NF отвергается.

---

### E3 — Model D: конфигурация, NRF, SCP (то, что уже в коде)

Большая часть EPIC 4/5/7 LAB-документа — уже реализованная функциональность. Здесь она превращается в конфиг-профиль и тесты.

| ID | Задача | Тип | Prio | Дни | Источник | Статус |
|---|---|---|---|---|---|---|
| E3-01 | Профиль `configs/model-d/`: во всех консьюмерах `client.scp` единственный; явно `delegated: {nrf: {nfm: no, disc: yes}, scp: {next: yes}}`; NRF-URI только у SCP, NRF и NSSF (`nrfId`) | CFG | P0 | 1 | Phase 1.1–1.2, LAB-040..044 | ✅ код |
| E3-02 | Тест delegated discovery: в pcap запрос консьюмера к SCP содержит `3gpp-Sbi-Discovery-target-nf-type`, `-service-names`, `-requester-nf-type`; SCP делает `GET /nnrf-disc/v1/nf-instances`; ответ несёт `3gpp-Sbi-Producer-Id` | TEST | P0 | 1 | LAB-041, 045..047, 091..093 | ✅ код |
| E3-03 | Тесты Nnrf_NFManagement: register, heartbeat/expiry, PATCH update, deregister, status subscription + callback | TEST | P0 | 2 | LAB-060..066 | ✅ код |
| E3-04 | Callback validation в консьюмерах: неизвестная subscription, чужой `nfInstanceId`, повтор, после удаления | TEST + CODE (при найденных дырах) | P1 | 2 | LAB-067 | ⚠️ |
| E3-05 | NRF restart recovery: NF ре-регистрируются по heartbeat 404, SCP продолжает discovery | TEST | P1 | 1 | LAB-068 | ✅ код |
| E3-06 | Discovery filters PLMN / S-NSSAI / DNN — позитив/негатив | TEST | P1 | 1 | LAB-048, 049, 064 | ✅ код |
| E3-07 | Два экземпляра AUSF, UDM, PCF, SMF одновременно; выбор по priority/capacity | TEST | P1 | 1 | LAB-050, 051 | ⚠️ priority есть, load — нет (см. E6) |
| E3-08 | NRF: строгая валидация NFProfile по OpenAPI r19, reject неизвестных mandatory | CODE | P1 | 2 | LAB-061 | ⚠️ |
| E3-09 | SCP loop protection (счётчик hop / `Via`), контролируемая ошибка | CODE | P1 | 1 | LAB-098 | ❌ |

**DoD E3:** профиль `model-d` — единственный default; все тесты E3 зелёные.

---

### E4 — Отвязка PCF от MongoDB

| ID | Задача | Тип | Prio | Дни | Источник |
|---|---|---|---|---|---|
| E4-01 | UDR: расширить `Nudr_DR /policy-data/ues/{supi}/am-data` и `/sm-data` — UE-AMBR, QoS-профили по DNN/S-NSSAI, PCC-правила из существующей Mongo-схемы. Файлы: `src/udr/nudr-handler.c`, `lib/dbi/subscription.c`, `lib/dbi/session.c` | CODE | P0 | 4 | Phase 2.1 |
| E4-02 | PCF: заменить `ogs_dbi_subscription_data()` (`src/pcf/nudr-handler.c:67`) и `ogs_dbi_session_data()` (`src/pcf/context.c:1029`) разбором `AmPolicyData` / `SmPolicyData` из ответа UDR. Файлы: `src/pcf/nudr-build.c`, `nudr-handler.c`, `context.c`, `sm-sm.c` | CODE | P0 | 4 | Phase 2.2, LAB-016 |
| E4-03 | Удалить `libogsdbi` из `src/pcf/meson.build`, `db_uri` из `pcf.yaml.in`, инициализацию dbi из `src/pcf/init.c`/`context.c:44` | CODE | P0 | 0.5 | Phase 2.3 |
| E4-04 | Тест: PCF без сетевого доступа к MongoDB; AM + SM policy association проходят; SWAP-006 | TEST | P0 | 1 | Phase 2.4, SWAP-005/006 |
| E4-05 | Предложить PR в апстрим `open5gs/open5gs` | DOC | P1 | 1 | Phase 7.4 |

**DoD E4:** `ldd open5gs-pcfd` не содержит `libogsdbi`; эталонный сценарий проходит.

---

### E5 — OAuth2 / Service Authorization (TS 33.501 §13.4, TS 29.510 §5.4) — критический путь

Состояние: реализация отсутствует полностью. В `lib/sbi/openapi/model` есть только сгенерированные `access_token_req`, `access_token_rsp`, `access_token_err`, `access_token_claims`. В `src/nrf`, `src/scp`, `lib/sbi` — ни одного упоминания oauth/token.

| ID | Задача | Тип | Prio | Дни | Источник |
|---|---|---|---|---|---|
| E5-01 | Gap-анализ зафиксирован (этот раздел). Выбор JWT-библиотеки: minimal собственная (`lib/sbi/jwt.c`, HS256 + ES256/RS256 через OpenSSL) vs `libjwt` как subproject. Решение — собственная, чтобы не тянуть зависимость в AGPL-дерево | DOC/CODE | P0 | 2 | LAB-070 |
| E5-02 | NRF: `POST /oauth2/token`, grant `client_credentials`; вход `AccessTokenReq` (`nfInstanceId`, `nfType`, `targetNfType`, `scope`, `targetNfInstanceId`, `requesterPlmn`); проверка, что requester зарегистрирован; выдача JWT с claims `iss, sub, aud, scope, exp, producerNfSetId`. Файлы: `src/nrf/nnrf-handler.c`, `src/nrf/sbi-path.c`, `src/nrf/context.c` (ключи, TTL), `configs/open5gs/nrf.yaml.in` (`oauth2: {enabled, issuer, signing_key, token_ttl}`) | CODE | P0 | 6 | Phase 3.2, LAB-071, 072 |
| E5-03 | Общий middleware валидации в `lib/sbi/server.c` / `lib/sbi/nghttp2-server.c`: `Authorization: Bearer`, подпись, `exp`, `aud` = свой nfType/instanceId, `scope` ⊇ service name; ответ `401` с `WWW-Authenticate: Bearer error="invalid_token"` и `403` для scope по 29.500 §6.7.3; исключение для `/nnrf-nfm`, `/oauth2/token` | CODE | P0 | 5 | Phase 3.3, LAB-073..075 |
| E5-04 | CCA (Client Credentials Assertion, 33.501 §13.3.8): консьюмер подписывает JWT `{sub: nfInstanceId, iat, exp, aud: [NRF, SCP]}` своим ключом и шлёт `3gpp-Sbi-Client-Credentials`; конфиг `oauth2.client_key` во всех NF. Файлы: `lib/sbi/client.c`, `lib/sbi/context.c`, `lib/sbi/message.h` (новый custom header) | CODE | P0 | 3 | NEW (Phase 3.5 уточнено) |
| E5-05 | SCP в Model D: по CCA консьюмера запрашивает токен у NRF с `targetNfType` из `3gpp-Sbi-Discovery-target-nf-type`; кэш токенов по ключу (consumerId, targetNfType, scope), refresh за N сек до `exp`; проброс `Authorization` продюсеру. Файлы: `src/scp/sbi-path.c`, `src/scp/context.c`, `src/scp/scp-sm.c` | CODE | P0 | 5 | Phase 3.4, LAB-076, 080 |
| E5-06 | NRF: проверка CCA при регистрации/discovery/token (публичные ключи NF из NFProfile или из конфига CA) | CODE | P0 | 2 | NEW |
| E5-07 | Negative tests: без токена, истёкший, чужой scope, чужой `aud`, поддельная подпись, чужой `nfInstanceId` в CCA, race при refresh | TEST | P0 | 2 | Phase 3.6, LAB-077..080 |
| E5-08 | Direct-mode (Model A/B) поддержка токена в `lib/sbi/client.c` — для swap-тестов со сторонними NF, которые не ходят через SCP | CODE | P1 | 2 | NEW |
| E5-09 | Ротация ключей NRF (`kid` в JWT, два активных ключа) | CODE | P2 | 2 | NEW |

**DoD E5:** запрос к продюсеру без валидного Bearer получает 401; полный сценарий с токенами проходит; в pcap видны `/oauth2/token` от SCP и `3gpp-Sbi-Client-Credentials` от консьюмеров.

---

### E6 — Binding, NF Set, Load/Overload control, SCP domains *(отсутствовало в обоих исходных планах)*

Без этого EPIC невозможны корректные reselection (LAB-052/097), учёт нагрузки (LAB-050) и chained SCP (LAB-099).

| ID | Задача | Тип | Prio | Дни | Источник |
|---|---|---|---|---|---|
| E6-01 | NFProfile: `nfSetIdList`, `nfServiceSetId`, `scpDomains`, `scpInfo` в `ogs_sbi_nf_instance_t` / `ogs_sbi_nf_service_t`; конфиг `nf_set_id:` в yaml каждого NF. Файлы: `lib/sbi/context.c/.h`, `lib/sbi/nnrf-build.c` | CODE | P1 | 3 | Phase 4.1, LAB-045 (NF Set) |
| E6-02 | `3gpp-Sbi-Binding` (29.500 §6.12.2): продюсер добавляет в ответ, создающий ресурс (`bl=nf-set; nfset=...` / `bl=nf-instance; nfinst=...`); консьюмер хранит binding в контексте сессии (`amf_sess_t`, `smf_sess_t`, `pcf_sess_t`) | CODE | P1 | 4 | Phase 4.2 |
| E6-03 | `3gpp-Sbi-Routing-Binding`: консьюмер отправляет при последующих запросах вместо/вместе с `3gpp-Sbi-Target-apiRoot`; SCP маршрутизирует по нему | CODE | P1 | 3 | Phase 4.2 |
| E6-04 | SCP reselection: при 5xx/timeout продюсера — выбор другого экземпляра того же NF Set из кэша discovery; retry только идемпотентных (GET/PUT/DELETE) и POST с `3gpp-Sbi-Retry` семантикой | CODE | P1 | 4 | Phase 4.3, LAB-052, 096, 097 |
| E6-05 | `3gpp-Sbi-Oci` / `3gpp-Sbi-Lci` (29.500 §6.3, §6.4): продюсер выдаёт по порогам (число сессий, CPU); SCP учитывает при выборе и снижает трафик по OCI. Файлы: `lib/sbi/server.c`, `src/scp/context.c`, метрики из `src/*/metrics.c` | CODE | P1 | 4 | Phase 4.4, LAB-050 |
| E6-06 | scpDomains: SCP регистрирует домен; discovery учитывает `scp-domain`; next-hop SCP по домену (клиентская часть уже есть: `delegated.scp.next`) | CODE | P1 | 3 | Phase 4.4, LAB-099 |
| E6-07 | Тест: два SMF в одном NF Set, kill активного во время PDU Session Modification — сессия продолжается на втором | TEST | P1 | 2 | Phase 4.5 |

**DoD E6:** тест E6-07 зелёный; в pcap видны `Binding`/`Routing-Binding`.

---

### E7 — Обработка ошибок и ProblemDetails (RFC 9457, TS 29.500 §5.2.7)

| ID | Задача | Тип | Prio | Дни | Источник |
|---|---|---|---|---|---|
| E7-01 | Матрица кодов ответов по API: success / client / server / retryable / `cause` — из TS 29.5xx в `docs/error-matrix.md` | DOC | P0 | 2 | LAB-100 |
| E7-02 | Валидация ProblemDetails в E0-04: `status` = HTTP status, `cause` из перечня TS, `type`/`title` не пусты | TEST | P0 | 1 | LAB-101 |
| E7-03 | Negative-сценарии: unknown SUPI (UDM/UDR → 404 `USER_NOT_FOUND`), unknown NF/service (NRF → 404/SCP → 500/504 с `cause`), malformed JSON (400 `INVALID_MSG_FORMAT`), unsupported API version (`3gpp-Sbi-*` / 404), timeout (SCP → 504 `TARGET_NF_NOT_REACHABLE`) | TEST | P1 | 3 | LAB-102..106 |
| E7-04 | SCP: прозрачная передача ProblemDetails продюсера; собственные ошибки только по 29.500 §6.10.8 (`NF_DISCOVERY_FAILURE`, `NF_SELECTION_FAILURE`, `TARGET_NF_NOT_REACHABLE`, `NRF_NOT_REACHABLE`) | CODE | P1 | 2 | LAB-094, 095, 107 |
| E7-05 *(v1.1)* | **`cause` во всех ProblemDetails.** Сейчас NULL в 223 из 238 вызовов `ogs_sbi_server_send_error()` в NRF/SCP/AMF/UDM. Ввести таблицу cause по TS (29.503 §6.1.7.3: `USER_NOT_FOUND`, `DATA_NOT_FOUND`, `UNSUPPORTED_RESOURCE_URI`, `AUTHENTICATION_REJECTED`, `SERVING_NETWORK_NOT_AUTHORIZED`; 29.510 §6.1.6.3: `INVALID_QUERY_PARAM`, `MANDATORY_IE_MISSING`; 29.518 §6.1.7.3: `CONTEXT_NOT_FOUND`, `UE_NOT_REACHABLE`, `TEMPORARY_REJECT_REGISTRATION_ONGOING`) и заполнить вызовы; добавить `invalidParams` для 400. Проверка: валидатор E0-04 требует `cause` при 4xx | CODE | **P0** | 4 | матрица §0 п.3 |

---

### E8 — UPF / PFCP (N4) — в основном подтверждение стандартности

| ID | Задача | Тип | Prio | Дни | Источник |
|---|---|---|---|---|---|
| E8-01 | UPF отдельный runtime, N4 на отдельном IP/интерфейсе, N6 через `ogstun` в своём netns | CFG | P0 | 1 | LAB-110 |
| E8-02 | Тесты PFCP Association (Setup, Heartbeat, Release, Recovery Time Stamp), Session Establishment/Modification/Deletion с PDR/FAR/QER/URR, валидация IE по 29.244 в tshark | TEST | P0 | 3 | LAB-111..115, 132 |
| E8-03 | UPF restart: SMF по Recovery Time Stamp сбрасывает сессии, UE переустанавливает | TEST | P1 | 1 | LAB-116 |
| E8-04 | Swap-тест: Open5GS SMF ↔ сторонний UPF (например, UPG-VPP или eUPF) | TEST | P1 | 2 | LAB-117, SWAP-008 |

---

### E9 — CHF и Nchf_ConvergedCharging (TS 32.291)

Состояние: у SMF нет Nchf-клиента; тарификация через Diameter Gy (`src/smf/gy-path.c`, `gy-handler.c`). LAB-122 («убрать жёсткий CHF URI из SMF») ставит проблему неверно.

| ID | Задача | Тип | Prio | Дни | Источник |
|---|---|---|---|---|---|
| E9-01 | Стаб-CHF: отдельный сервис по `TS32291_Nchf_ConvergedCharging.yaml` из `lib/sbi/openapi/support/r19-*`; регистрация в NRF как `CHF`; Create/Update/Release с квотами и триггерами; OAuth2-валидация по E5-03 (можно на C поверх `lib/sbi` или отдельно на Go/Python — не важно для swap-теста) | NEW | P1 | 5 | Phase 5.1, LAB-120, 121 |
| E9-02 | SMF: `nchf-build.c` / `nchf-handler.c`; маппинг логики Gy (`gy-handler.c`: квоты, триггеры, usage) на `ChargingDataRequest/Response`; discovery CHF через SCP (`3gpp-Sbi-Discovery-target-nf-type: CHF`); URR-репорты из PFCP → Update | CODE | P1 | 8 | Phase 5.2, LAB-122..125 |
| E9-03 | Конфиг `smf.charging.mode: nchf | gy | none`; Gy остаётся для EPC-профиля | CODE | P1 | 1 | Phase 5.3 |
| E9-04 | Тесты: create → квота → исчерпание → update → release; failure (timeout, 503, malformed) → поведение по 32.291 (continue/terminate по конфигу); schema validation | TEST | P1 | 3 | Phase 5.4, LAB-126, 127 |

---

### E10 — Недостающие NF (стабы для полноты топологии)

| ID | NF | Подход | Тип | Prio | Дни | Источник |
|---|---|---|---|---|---|---|
| E10-01 | AF | Клиент `Npcf_PolicyAuthorization` (PCF уже отдаёт нотификации: `src/pcf/naf-build.c`) | NEW | P2 | 3 | Phase 6.4 |
| E10-02 | NEF | Стаб `Nnef_EventExposure` → подписки `Namf_EventExposure` / `Nsmf_EventExposure` | NEW | P2 | 5 | Phase 6.1 |
| E10-03 | NWDAF | Стаб `Nnwdaf_AnalyticsInfo` (NF load) на данных Prometheus-метрик AMF/SMF/UPF | NEW | P2 | 4 | Phase 6.2 |
| E10-04 | SMSF | `Nsmsf_SMService` + Nudm_UECM; в AMF — NAS SMS transport (`src/amf/gmm-handler.c`) | NEW + CODE | P2 | 8 | Phase 6.3 |
| E10-05 | LMF, UDSF, N3IWF | Регистрация в NRF без функционала (только для discovery/swap-тестов топологии) | NEW | P2 | 2 | Phase 6.5 |

---

### E11 — SEPP, N32, второй PLMN

Состояние: `src/sepp` реализован; `sepp1.yaml.in`/`sepp2.yaml.in` содержат N32-c/N32-f по TLS. Это конфиг + тесты, не разработка (в отличие от оценки LAB EPIC 14).

| ID | Задача | Тип | Prio | Дни | Источник |
|---|---|---|---|---|---|
| E11-01 | Второй PLMN (999/71): свои NRF, SCP, AUSF/UDM/UDR, DNS-зона `5gc-b.lab`, CA | CFG | P2 | 2 | LAB-150, 151 |
| E11-02 | SEPP-A / SEPP-B по существующим конфигам; N32-c handshake, N32-f TLS; discovery hPLMN через `3gpp-Sbi-Discovery-target-plmn-list` | CFG/TEST | P2 | 2 | LAB-152..155 |
| E11-03 | Roaming с OAuth2 (E5): токен с `requesterPlmn`/`targetPlmn`, NRF-to-NRF через SEPP | CODE | P2 | 3 | Phase 7.2, LAB-156 |
| E11-04 | PRINS (29.573 §5.3) — только если нужен IPX-сценарий | CODE | P2 | 10 | NEW |

---

### E12 — Observability

| ID | Задача | Тип | Prio | Дни | Источник |
|---|---|---|---|---|---|
| E12-01 | `3gpp-Sbi-Correlation-Info` (29.500 §5.2.3.2.x) генерируется AMF/SMF, пробрасывается SCP, логируется всеми NF; сквозная трасса AMF→SCP→NRF→AUSF→UDM→UDR | CODE | P1 | 3 | LAB-140 |
| E12-02 | SBI access log в `lib/sbi/server.c` / `client.c`: timestamp, method, URI, status, consumer/producer nfInstanceId, latency, correlation | CODE | P1 | 2 | LAB-141 |
| E12-03 | SCP: лог решения discovery (параметры, кандидаты, выбранный, причина) | CODE | P1 | 1 | LAB-142 |
| E12-04 | Корреляция PFCP SEID ↔ SUPI ↔ PDU Session ID в логах SMF/UPF | CODE | P1 | 1 | LAB-143 |
| E12-05 | Grafana: registered NF, SBI rate/errors/latency, discovery failures, PFCP sessions, UE/PDU — поверх существующих `src/*/metrics.c` | CFG | P2 | 2 | LAB-146 |
| E12-06 | OTLP export | CODE | P2 | 3 | LAB-145 |

---

### E13 — Swap-тесты (главный критерий приёмки)

Каждый swap-тест: заменить Open5GS NF-X сторонней реализацией, прогнать эталонный сценарий E0-06 + negative-набор, убедиться, что изменялись только конфиг/DNS/сертификаты/NRF-профиль.

| ID | NF | Кандидаты сторонней реализации | Prio | Зависит от |
|---|---|---|---|---|
| SWAP-001 | NRF | free5GC NRF, sdcore | P0 | E5 (токены), E3 |
| SWAP-002 | SCP | free5GC/другой SCP с Model D | P1 | E6 |
| SWAP-003 | AUSF | free5GC AUSF | P0 | E5-08 |
| SWAP-004 | UDM | free5GC UDM | P0 | E5-08 |
| SWAP-005 | UDR | free5GC UDR | P0 | E4 |
| SWAP-006 | PCF | free5GC PCF | P0 | E4 |
| SWAP-007 | SMF | free5GC SMF | P1 | E6 |
| SWAP-008 | UPF | UPG-VPP, eUPF, free5GC UPF | P1 | E8 |
| SWAP-009 | NSSF | free5GC NSSF | P1 | — |
| SWAP-010 | BSF | free5GC/sdcore | P1 | — |
| SWAP-011 | CHF | сторонний/второй стаб | P1 | E9 |
| SWAP-012 *(NEW)* | AMF | free5GC AMF с UERANSIM | P1 | E6 |

Примечание: сторонние NF могут не поддерживать Model D / OAuth2 — для них SCP должен уметь Model C (discovery делегирован, но продюсер выбирается консьюмером) — уже поддерживается через `delegated.nrf.disc: no`.

---

## 6. Тест-матрица (сжатая)

| Область | Кейсы | Каталог |
|---|---|---|
| SBI schema | каждый request/response против OpenAPI r19; ProblemDetails | `tests/sbi` |
| NRF | register / heartbeat / update / deregister / subscribe / restart | `tests/nrf` |
| SCP | discovery headers, target type/service/PLMN/S-NSSAI, reselection, loop, error passthrough | `tests/scp` |
| OAuth2 | token issue, CCA, expired, wrong scope/aud/signature, cache/refresh | `tests/oauth` |
| TLS | expired, unknown CA, wrong SAN, TLS<1.2, ALPN | `tests/negative/tls` |
| PFCP | association, session CRUD, URR, restart | `tests/pfcp` |
| NGAP/NAS/GTP-U | эталонный сценарий, валидация в tshark | `tests/ngap`, `tests/nas`, `tests/gtpu` |
| Charging | create/update/release, failures | `tests/charging` |
| Interop | SWAP-001..012 | `tests/interop` |
| DNS | NXDOMAIN, timeout, SERVFAIL | `tests/negative/dns` |

---

## 7. Milestones, зависимости, оценка

```text
E0 ──► E1 ──► E2 ──► E3 ──┬──► E4 ──────────────┐
                          │                     ├──► E13 (swap P0) ──► M-MVP
                          └──► E5 ──► E6 ──► E7 ┘
                                       │
                                       ├──► E9 ──► E13 (swap P1)
                                       ├──► E11
                                       └──► E10, E12
```

| Milestone | Содержит | Готовность |
|---|---|---|
| **M1 Isolation** | E0, E1 | каждая NF отдельный runtime, FQDN, без loopback |
| **M2 Secure transport** | E2, E3 | https/mTLS/HTTP2 везде, профиль `model-d` default, тесты NRF/SCP |
| **M3 Data ownership** | E4 | PCF без MongoDB, SWAP-005/006 |
| **M4 Service authorization** | E5 | OAuth2 + CCA end-to-end, negative tests |
| **M5 Resilient Model D** | E6, E7 | binding, reselection, Oci/Lci, error matrix |
| **M6 = MVP** | SWAP-001, 003..006 | внешние NRF/AUSF/UDM/UDR/PCF проходят эталонный сценарий |
| **M7 Extensions** | E8, E9, E10, E11, E12, остальные SWAP | CHF, стабы, roaming, observability |

**Критический путь:** E5 (OAuth2, ~25 дней) → E6 (binding, ~23 дня) → SWAP.

**Оценка:** P0 ≈ 65 дней, P1 ≈ 75 дней, P2 ≈ 50 дней. Один разработчик — 4,5–5 месяцев до M6, ~7 месяцев до M7; два разработчика (один на E5/E6, второй на E1–E4/E7–E9) — ~3 месяца до M6.

---

## 8. MVP: acceptance checklist

- [ ] UE Registration, 5G-AKA, PDU Session Establishment, пользовательский трафик через UPF
- [ ] Ни один консьюмер не содержит URI продюсера (кроме `nrfId` в NSSF)
- [ ] Каждый SBI-запрос идёт через SCP с `3gpp-Sbi-Discovery-*`, ответ с `3gpp-Sbi-Producer-Id`
- [ ] Все SBI-соединения — TLS 1.2+/HTTP2, mTLS, индивидуальные сертификаты, SAN = FQDN
- [ ] Каждый запрос к продюсеру несёт валидный Bearer; без него — 401
- [ ] PCF не линкует `libogsdbi`, работает без доступа к MongoDB
- [ ] Валидатор OpenAPI/ProblemDetails без ошибок на эталонном сценарии
- [ ] SWAP-001, 003, 004, 005, 006 пройдены
- [ ] pcap + SBI-журнал сохраняются на каждый прогон

---

## Приложение A. Перекрёстная таблица LAB-xxx → объединённый план

| LAB / SWAP | Решение | Новый ID |
|---|---|---|
| LAB-001 | Переопределено: baseline Rel-19 схемы, а не Rel-18 | E0-02 |
| LAB-002, 003, 005 | Принято | E0-02, E0-03 |
| LAB-004 | Принято, см. §3.4 | — |
| LAB-010..015 | Принято; аудит DB уже выполнен | E1-01..05 |
| LAB-016 | Принято, детализировано по файлам | E4-01..04 |
| LAB-017 | Принято как тест | E1-06 |
| LAB-018 | Объединено со swap-тестами | E13 |
| LAB-020..022 | Принято | E1-03, E1-07 |
| LAB-023..025 | **Отклонено**: SRV/NAPTR не применимы к 5GC SBA | — |
| LAB-030..038 | Принято | E2-01..06 |
| LAB-040 | Принято | E3-01 |
| LAB-041..044 | Переклассифицировано CODE → CFG/TEST: уже реализовано | E3-01, E3-02 |
| LAB-045..047 | Принято как тесты | E3-02 |
| LAB-048..049 | Принято как тесты | E3-06 |
| LAB-050 | Разделено: priority — тест (E3-07), load — код (E6-05) | E3-07, E6-05 |
| LAB-051 | Принято | E3-07 |
| LAB-052 | Требует E6 (binding) — переклассифицировано в CODE | E6-04 |
| LAB-060..068 | Принято как тесты; 061, 067 — с кодом | E3-03..08 |
| LAB-070 | Аудит выполнен: реализации нет | E5-01 |
| LAB-071..080 | Принято, детализировано; добавлены CCA и NRF-side проверка | E5-02..09 |
| LAB-090 | **Отклонено**: SCP уже standalone | — |
| LAB-091..093 | Тесты | E3-02 |
| LAB-094, 095, 107 | Принято | E7-04 |
| LAB-096, 097 | Требуют E6 | E6-04 |
| LAB-098 | Принято | E3-09 |
| LAB-099 | Требует scpDomains | E6-06 |
| LAB-100..106 | Принято | E7-01..03 |
| LAB-110..117 | Принято | E8-01..04 |
| LAB-120, 121 | Принято | E9-01 |
| LAB-122 | Переформулировано: у SMF нет Nchf-клиента вообще | E9-02 |
| LAB-123..127 | Принято | E9-02..04 |
| LAB-130..136 | Принято | E0-04, E0-05, E0-07 |
| LAB-140..146 | Принято | E12-01..06 |
| LAB-150..156 | Переклассифицировано: SEPP/N32 уже в коде → CFG/TEST | E11-01..03 |
| SWAP-001..011 | Принято без изменений; добавлен SWAP-012 (AMF) | E13 |

## Приложение B. Задачи, отсутствовавшие в обоих исходных планах

E5-04 (CCA), E5-06, E5-08, E5-09, E6-01..07 (весь EPIC binding/NF Set/Oci-Lci/scpDomains), E11-04 (PRINS), E12-01 (Correlation-Info), SWAP-012, RULE-006/007.

## Приложение C. Ключевые точки входа в код

| Область | Файлы |
|---|---|
| SBI сервер / middleware | `lib/sbi/server.c`, `lib/sbi/nghttp2-server.c` |
| SBI клиент / custom headers | `lib/sbi/client.c`, `lib/sbi/message.h`, `lib/sbi/path.c` |
| Delegated config | `lib/sbi/context.h` (`ogs_sbi_client_delegated_config_t`), `lib/sbi/context.c` |
| NRF | `src/nrf/nnrf-handler.c`, `src/nrf/sbi-path.c`, `src/nrf/context.c` |
| SCP | `src/scp/sbi-path.c`, `src/scp/context.c`, `src/scp/scp-sm.c` |
| PCF ↔ UDR | `src/pcf/nudr-build.c`, `src/pcf/nudr-handler.c`, `src/pcf/context.c`, `src/udr/nudr-handler.c` |
| SMF charging | `src/smf/gy-path.c`, `src/smf/gy-handler.c` → новые `nchf-build.c`, `nchf-handler.c` |
| SEPP | `src/sepp/*`, `configs/open5gs/sepp1.yaml.in`, `sepp2.yaml.in` |
| OpenAPI-схемы | `lib/sbi/openapi/support/r19-20260402-openapitools-7.20.0/` |
| Метрики | `src/{amf,smf,upf,pcf}/metrics.c` |

---

## 9. Дополнение v1.1 — по результатам матрицы соответствия

### E14 — NRF: сохранность профилей и discovery (матрица §1.2, §1.3)

Состояние: `ogs_sbi_nnrf_handle_nf_profile()` (`lib/sbi/nnrf-handler.c`) переносит NFProfile в `ogs_sbi_nf_instance_t`, сохраняя фиксированный набор полей; `ogs_nnrf_nfm_build_nf_profile()` (`lib/sbi/nnrf-build.c`) собирает профиль обратно. Всё, что не в наборе, теряется между регистрацией и discovery.

| ID | Задача | Тип | Prio | Дни | Файлы |
|---|---|---|---|---|---|
| E14-01 | **NRF хранит исходный NFProfile как JSON** (`cJSON *raw_profile` в `ogs_sbi_nf_instance_t` или отдельная таблица в `src/nrf/context.c`). Discovery/Retrieval отдают исходный JSON с применённым PATCH; внутренняя структура используется только для фильтрации и таймеров. PATCH применяется к JSON (RFC 6902: `add`/`remove`/`replace`/`test`) | CODE | **P0** | 5 | `src/nrf/context.c/.h`, `src/nrf/nnrf-handler.c`, `nnrf-build.c` |
| E14-02 | Парсинг в структуру `udrInfo`, `udmInfo`, `ausfInfo`, `pcfInfo`, `chfInfo`, `bsfInfo`, `nssfInfo`, `upfInfo` (`supiRanges`, `gpsiRanges`, `routingIndicators`, `groupId`, `dnnList`, `plmnRangeList`) — для фильтрации | CODE | **P0** | 4 | `lib/sbi/context.h` (`ogs_sbi_nf_info_t`), `lib/sbi/nnrf-handler.c` |
| E14-03 | Discovery-фильтры: `supi` (по `*Info.supiRanges`), `routing-indicator` (по `udmInfo/ausfInfo.routingIndicators`), `nf-set-id`, `nf-service-set-id`, `group-id-list`, `preferred-locality` (сортировка), `supported-features` | CODE | **P0** | 4 | `lib/sbi/context.c:2155`, `src/nrf/nnrf-handler.c:1051` |
| E14-04 | SCP пробрасывает те же параметры из `3gpp-Sbi-Discovery-supi`, `-routing-indicator`, `-nf-set-id`, `-preferred-locality`; AMF передаёт `routing-indicator` из SUCI при discovery AUSF/UDM; UDM — при discovery UDR передаёт `supi`/`group-id` | CODE | **P0** | 3 | `src/scp/sbi-path.c`, `src/amf/sbi-path.c`, `src/amf/nausf-build.c`, `src/udm/sbi-path.c` |
| E14-05 | NFStatusSubscribe: все `subscrCond` (`AmfCond`, `GuamiListCond`, `NfGroupCond`, `NfSetCond`, `NfServiceSetCond`, `SnssaiCond`, `UpfCond`, `ScpDomainCond`), `notifCondition`; нотификация `NF_PROFILE_CHANGED` с `profileChanges` (JSON Patch) | CODE | P1 | 4 | `src/nrf/nnrf-handler.c:481`, `src/nrf/nnrf-build.c` |
| E14-06 | `SearchResult.nrfSupportedFeatures`, `numNfInstComplete`; `GET /nf-instances` с `page-number`/`page-size`; ETag на профилях | CODE | P2 | 2 | |
| E14-07 | `GET /bootstrapping` (29.510 §6.4) | CODE | P2 | 1 | |
| E14-08 | Тест: регистрация профилей вендорских SMF/PCF/CHF/UDR (взять реальные JSON из pcap) → discovery возвращает их байт-в-байт (кроме `nfStatus`/`load`) | TEST | **P0** | 1 | `tests/nrf` |

**DoD E14:** профиль любого NF проходит через NRF без потерь; `supi=` и `routing-indicator=` сужают результат.

### E15 — AMF: пробелы, важные для вендорских соседей (матрица §3)

| ID | Задача | Тип | Prio | Дни | Файлы |
|---|---|---|---|---|---|
| E15-01 | Отказ PCF при AM policy association не должен валить регистрацию UE: конфиг `amf.pcf.mandatory: false` → продолжать без политики (23.502 §4.2.2.2.2 допускает) | CODE | **P0** | 1 | `src/amf/gmm-sm.c`, `npcf-handler.c` |
| E15-02 | Namf_Communication `AMFStatusChangeSubscribe/Unsubscribe/Notify` | CODE | P1 | 3 | `src/amf/amf-sm.c`, `namf-handler.c` |
| E15-03 | Namf_EventExposure: `subscriptions` с событиями `REACHABILITY_REPORT`, `LOCATION_REPORT`, `REGISTRATION_STATE_REPORT`, `CONNECTIVITY_STATE_REPORT` | CODE | P1 | 6 | новые `namf-evex-*.c` |
| E15-04 | Namf_MT `enable-ue-reachability` + `ue-reachind` | CODE | P1 | 2 | |
| E15-05 | UL/DL NAS Transport: payload container `SMS` (→ Nsmsf_SMService), `UE policy container` (→ Npcf_UEPolicyControl), `UPU`/`SOR` (→ UDM ack) | CODE | P1 | 6 | `src/amf/gmm-handler.c`, `gmm-build.c` |
| E15-06 | Npcf_UEPolicyControl (`/npcf-ue-policy-control/v1/policies`) | CODE | P1 | 3 | новые `npcf-ue-*.c` |
| E15-07 | Npcf_AMPolicyControl `PATCH /policies/{id}/update` с trigger report (`LOC_CH`, `PRA_CH`, `SERV_AREA_CH`) | CODE | P1 | 2 | `src/amf/npcf-build.c` |
| E15-08 | NGAP: `OverloadStart/Stop`, `AMFStatusIndication`, обработка `AMFConfigurationUpdateAcknowledge/Failure`, `NASNonDeliveryIndication`, `PDUSessionResourceNotify/ModifyIndication` | CODE | P1 | 5 | `src/amf/ngap-sm.c`, `ngap-handler.c`, `ngap-build.c` |
| E15-09 | EAP-AKA' в NAS (EAP message IE в Authentication Request/Response/Result) | CODE | P2 | 4 | `gmm-build.c`, `gmm-handler.c`, `nausf-handler.c` |
| E15-10 | N1N2MessageSubscribe/Unsubscribe; `Nsmf_PDUSession retrieve` | CODE | P2 | 3 | |
| E15-11 | `3gpp-Sbi-Binding` от SMF/PCF → хранение в `amf_sess_t`/`amf_ue_t` → `Routing-Binding` (связка с E6-02/03) | CODE | P1 | (учтено в E6) | |

### E16 — UDM: пробелы (матрица §4)

| ID | Задача | Тип | Prio | Дни | Файлы |
|---|---|---|---|---|---|
| E16-01 | Nudm_UECM `GET amf-3gpp-access`, `GET smf-registrations`, `GET /registrations`; `PUT/DELETE smsf-3gpp-access` | CODE | P1 | 3 | `src/udm/udm-sm.c`, `nudm-handler.c` |
| E16-02 | Nudm_SDM `shared-data`, `shared-data-subscriptions`; `sms-mng-data`, `sms-data`, `ue-context-in-smsf-data`; `id-translation-result` | CODE | P1 | 4 | |
| E16-03 | EAP-AKA' AV (`CK'/IK'` derivation, `authenticationMethod: EAP_AKA_PRIME`) | CODE | P2 | 2 | `src/udm/nudr-handler.c`, `lib/crypt/ogs-kdf.c` |
| E16-04 | Nudm_EE (subscriptions, `MonitoringConfiguration`) — нужен вендорским NEF/PCF | CODE | P2 | 5 | |
| E16-05 | SoR/UPU: `am-data/sor-ack`, `upu-ack`, генерация контейнеров (33.501 §6.14, §6.15) | CODE | P2 | 5 | |
| E16-06 | Подписка UDM на изменения данных в вендорском UDR (`subs-to-notify`) — иначе изменения провижена не доходят до AMF/SMF | CODE | P1 | 3 | `src/udm/nudr-build.c` |
| E16-07 | Тест UDM↔вендорский UDR: все 9 операций из матрицы §4.5 + `cause` при 404 | TEST | **P0** | 1 | `tests/interop` |

### 9.1 Профиль топологии «Open5GS = NRF + SCP + AMF + UDM»

При таком составе EPIC E4, E8, E9, E10, E11 исключаются; E1 сокращается до 4 контейнеров + DNS + CA. Пересобранный критический список:

| Порядок | Задачи | Дни | Что разблокирует |
|---|---|---|---|
| 1 | E14-01, 02, 03, 04, 08 | 17 | вендорские SMF/PCF/CHF/UDR видят друг друга через NRF с полными профилями; UDM/AUSF выбираются по routing indicator |
| 2 | E7-05 | 4 | вендорские NF корректно интерпретируют ошибки NRF/UDM/AMF |
| 3 | E2-01..03 | 2 | TLS/mTLS везде |
| 4 | E5-02, E5-05 (+E5-04 при необходимости) | 11–14 | OAuth2: вендорские NF получают токены; AMF/UDM ходят с Bearer через SCP |
| 5 | E15-01, E16-07, E0-04/05 | 7 | устойчивость регистрации; проверка UDM↔UDR; валидатор и pcap |
| 6 | E3-01, E3-08, E3-09 | 4.5 | профиль Model D, loop protection |
| **Итого P0** | | **≈ 47 дней** | |
| 7 (P1) | E5-03, E6-02/03/05, E14-05, E15-02..08, E16-01/02/06, E12-01/02 | ≈ 45 дней | строгость, NF Set, события, SMS/UE policy |

**Зависимости:** 1 и 2 независимы и могут идти параллельно; 4 после 3; 5–6 после 1.

**Ожидаемый эффект по вашим трём симптомам** (подтверждать по pcap):
- *вендорский CHF* — вендорский SMF не находит CHF: NRF отдаёт профиль без `chfInfo` → блок 1;
- *вендорский PCF* — AMF без токена/по plaintext, PCF отказывает, регистрация падает → блоки 2, 3, 4, E15-01; SMF не находит PCF по `supi` → блок 1;
- *вендорский SMF* — те же причины + Nudm_SDM `sm-data` из вендорского UDR (проверить блоком 5) + Binding при нескольких SMF (P1).

## Приложение D. Задачи, добавленные в v1.1

E7-05, E14-01..08, E15-01..11, E16-01..07; изменены статусы в §3.2/§3.3; раздел 9.1 — профиль четырёх NF.
