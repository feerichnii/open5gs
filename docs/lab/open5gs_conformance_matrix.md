# Матрица соответствия 3GPP: Open5GS v2.8.0 — NRF, SCP, AMF, UDM

> **lab/model-d (2026-09-14):** см. `docs/lab/acceptance-checklist.md` — реализованы E14 (raw NFProfile), E7-05 (`cause`), E15-01, базовый E5 OAuth2, discovery `supi`/`routing-indicator`, SCP loop guard.

**Метод:** статический аудит исходников `open5gs/open5gs` main @ `2d3bb46` (зеркало `feerichnii/open5gs`), 2026-09-14. Каждая строка — пункт TS → что реализовано → файл. Поведенческие проверки (по pcap) не проводились.

**Обозначения:** ✅ реализовано · ⚠️ частично / с оговоркой · ❌ отсутствует · 🔴 критично для взаимодействия с вендорскими NF · 🟡 желательно · ⚪ не нужно для вашей топологии

**Контекст:** от Open5GS берутся только NRF, SCP, AMF, UDM; SMF, PCF, CHF, UPF, AUSF, UDR, NSSF, BSF — вендорские.

---

## 0. Сводка критичных находок

| # | Находка | Влияние на вендорские NF | Файл |
|---|---|---|---|
| 1 | **NRF теряет большую часть NFProfile при регистрации.** Профиль парсится в `ogs_sbi_nf_instance_t` и при discovery собирается обратно только из сохранённых полей. Из `*Info` сохраняются только `amfInfo`, `smfInfo`, `scpInfo`, `seppInfo`. `udrInfo`, `udmInfo`, `ausfInfo`, `pcfInfo`, `chfInfo`, `bsfInfo`, `nssfInfo`, `upfInfo`, `nfSetIdList`, `nfServiceSetIdList`, `locality`, `supportedFeatures`, `oauth2Required`, `apiPrefix`, `allowedPlmns`, `defaultNotificationSubscriptions` — отбрасываются | 🔴 Вендорский SMF, запросивший у NRF PCF/CHF по `supi`, получит профили без `pcfInfo.supiRanges` / `chfInfo.supiRangeList` и отбракует их локально. Вендорский AUSF/AMF не найдёт UDM по `routingIndicator`. Это самая вероятная причина «PCF/CHF/SMF не работают» | `lib/sbi/nnrf-handler.c` (`ogs_sbi_nnrf_handle_nf_profile`), `lib/sbi/nnrf-build.c`, `lib/sbi/context.h` |
| 2 | **Nnrf_AccessToken отсутствует** (`/oauth2/token`) | 🔴 Вендорские NF, настроенные на OAuth2 (норма для commercial-grade), не получат токен ни для Open5GS UDM/AMF, ни друг для друга | `src/nrf/nrf-sm.c` |
| 3 | **ProblemDetails без `cause`** в 223 из 238 ошибочных ответов четырёх NF (UDM 114/120, AMF 33/33, NRF 60/62, SCP 16/23) | 🔴 Вендорские NF ветвят логику по `cause` (например `USER_NOT_FOUND`, `CONTEXT_NOT_FOUND`); без него — неопределённое поведение | все `*-handler.c` |
| 4 | **NRF discovery фильтрует только по** `target-nf-type`, `requester-nf-type`, `service-names`, `snssais`, `dnn`, `tai` (для SMF), `target/requester-plmn-list`, `hnrf-uri`. Параметры `supi`, `routing-indicator`, `nf-set-id`, `group-id-list`, `preferred-locality`, `supported-features` принимаются, но не применяются | 🔴 в связке с №1: результат discovery неотфильтрован и без данных для локальной фильтрации | `lib/sbi/context.c:2155` `ogs_sbi_discovery_option_is_matched` |
| 5 | **AMF UL NAS Transport принимает только payload type `N1 SM information`**; SMS, LPP, SOR, UE policy container, UE parameters update — не обрабатываются | 🟡 вендорские SMSF/PCF (UE policy) через Open5GS AMF работать не будут | `src/amf/gmm-handler.c` |
| 6 | **Заголовки 29.500 отсутствуют:** `3gpp-Sbi-Binding`, `Routing-Binding`, `Oci`, `Lci`, `Correlation-Info`, `Client-Credentials`, `Sender-Timestamp`, `Max-Rsp-Time`, `Response-Info` | 🟡 без Binding вендорские SMF/PCF с NF Set будут терять сессии при reselection | `lib/sbi/message.h` |
| 7 | **NRF NFStatusSubscribe** поддерживает `subscrCond` только `NfInstanceIdCond`, `NfTypeCond`, `ServiceNameCond`; нет `AmfCond`, `GuamiListCond`, `NfGroupCond`, `NfSetCond`, `NfServiceSetCond`, `SnssaiCond`, `UpfCond`, `ScpDomainCond`; нет `NF_PROFILE_CHANGED` нотификаций | 🟡 вендорский SMF, подписавшийся на изменения UPF по `UpfCond`, получит 400 | `src/nrf/nnrf-handler.c:587` |

---

## 1. NRF

### 1.1 Nnrf_NFManagement (TS 29.510 §5.2)

| Операция | Метод / путь | Статус | Комментарий | Файл |
|---|---|---|---|---|
| NFRegister | `PUT /nf-instances/{id}` | ✅ | 201/200; `heartBeatTimer` выдаётся NRF; `nfProfileChangesSupportInd` принимается | `src/nrf/nnrf-handler.c` `nrf_nnrf_handle_nf_register` |
| NFUpdate (full) | `PUT /nf-instances/{id}` | ✅ | | там же |
| NFUpdate (partial) | `PATCH /nf-instances/{id}` | ⚠️ | только `op: replace`; `add`/`remove`/`test`/`move` → 400. Пути: `/nfStatus`, `/load`, `/priority`, `/capacity` — прочие игнорируются | `nnrf-handler.c:342` |
| NFDeregister | `DELETE /nf-instances/{id}` | ✅ | нотификация `NF_DEREGISTERED` подписчикам | `src/nrf/nf-sm.c:231` |
| NFListRetrieval | `GET /nf-instances?nf-type=&limit=` | ✅ | `UriList`; `page-number`/`page-size` ❌ | `nrf_nnrf_handle_nf_list_retrieval` |
| NFProfileRetrieval | `GET /nf-instances/{id}` | ✅ | ETag/`If-None-Match` ❌ | `nrf_nnrf_handle_nf_profile_retrieval` |
| NFStatusSubscribe | `POST /subscriptions` | ⚠️ | `validityTime` выдаётся; `subscrCond` — 3 из 11 видов (см. сводка №7); `reqNfType`, `reqNfFqdn`, `nrfSupportedFeatures` ✅; `notifCondition` (monitoredAttributes/unmonitoredAttributes) ❌ | `nnrf-handler.c:481` |
| NFStatusSubscribeModify | `PATCH /subscriptions/{id}` | ⚠️ | только `/validityTime` replace | `nnrf-handler.c:792` |
| NFStatusUnsubscribe | `DELETE /subscriptions/{id}` | ✅ | | |
| NFStatusNotify (client) | `POST {nfStatusNotificationUri}` | ⚠️ | `NF_REGISTERED`, `NF_DEREGISTERED` ✅; `NF_PROFILE_CHANGED` с `profileChanges` ❌ | `src/nrf/nnrf-build.c:73` |
| `OPTIONS` | | ✅ | | `nrf-sm.c` |
| Nnrf_Bootstrapping | `GET /bootstrapping` | ❌ | TS 29.510 §6.4 | — |

### 1.2 NFProfile — сохраняемые поля (TS 29.510 Table 6.1.6.2.2-1)

| Поле | Статус | Поле | Статус |
|---|---|---|---|
| `nfInstanceId`, `nfType`, `nfStatus` | ✅ | `nfSetIdList` | ❌ 🔴 |
| `heartBeatTimer` | ✅ | `nfServiceSetIdList` (в NFService) | ❌ |
| `plmnList`, `sNssais`, `allowedNssais`, `allowedNfTypes` | ✅ | `perPlmnSnssaiList`, `nsiList` | ❌ |
| `fqdn`, `ipv4Addresses`, `ipv6Addresses` | ✅ | `locality`, `priority`/`capacity`/`load` | ✅ (locality ❌) |
| `amfInfo` / `amfInfoList` | ✅ | `udrInfo`, `udmInfo`, `ausfInfo` | ❌ 🔴 |
| `smfInfo` / `smfInfoList` | ✅ | `pcfInfo`, `chfInfo`, `bsfInfo` | ❌ 🔴 |
| `scpInfo`, `seppInfo` | ✅ | `nssfInfo`, `upfInfo`, `nefInfo`, `nwdafInfo` | ❌ |
| `nfServices` / `nfServiceList` | ⚠️ (см. ниже) | `nfProfileChangesSupportInd` | ✅ |
| `customInfo`, `recoveryTime` | ❌ | `nfServicePersistence`, `snpnList`, `supportedFeatures` | ❌ |

NFService: сохраняются `serviceInstanceId`, `serviceName`, `versions`, `scheme`, `nfServiceStatus`, `fqdn`, `ipEndPoints`, `allowedNfTypes`, `priority`, `capacity`, `load`. Отбрасываются `apiPrefix`, `supportedFeatures`, `allowedPlmns`, `allowedNssais`, `oauth2Required`, `defaultNotificationSubscriptions`, `nfServiceSetIdList`, `interPlmnFqdn`, `perPlmnSnssaiList`.

**Вывод:** NRF не является прозрачным хранилищем профилей. Правильное исправление — хранить исходный JSON профиля (cJSON) и отдавать его в discovery/retrieval без пересборки, применяя фильтры поверх.

### 1.3 Nnrf_NFDiscovery (TS 29.510 §5.3)

| Параметр запроса (Table 6.2.3.2.3.1-1) | Принимается | Применяется в фильтре |
|---|---|---|
| `target-nf-type`, `requester-nf-type` | ✅ | ✅ |
| `service-names` | ✅ | ✅ |
| `requester-nf-instance-id`, `target-nf-instance-id` | ✅ | ✅ |
| `snssais`, `dnn` | ✅ | ✅ (по `smfInfo`/`sNssais`) |
| `tai` | ✅ | ✅ (только SMF `taiList`) |
| `guami` | ✅ | ⚠️ по `amfInfo.guamiList` |
| `target-plmn-list`, `requester-plmn-list` | ✅ | ✅ |
| `hnrf-uri`, `requester-features`, `limit` | ✅ | ✅ |
| `supi` | ✅ | ❌ 🔴 (нет `*Info.supiRanges`) |
| `routing-indicator` | ❌ | ❌ 🔴 (UDM/AUSF selection по SUCI) |
| `nf-set-id`, `nf-service-set-id` | ❌ | ❌ |
| `group-id-list`, `dnai-list`, `pdu-session-types` | ❌ | ❌ |
| `preferred-locality`, `preferred-nf-instances`, `preferred-api-versions` | ❌ | ❌ |
| `supported-features`, `ip-domain`, `chf-supported-plmn` | ❌ | ❌ |
| `scp-domain-list`, `serving-scope`, `access-type` | ❌ | ❌ |
| `gpsi`, `external-group-identity`, `data-set`, `upf-iwk-eps-ind` | ❌ | ❌ |

Ответ `SearchResult`: `validityPeriod` ✅, `nfInstances` ⚠️ (пересобранные профили), `nrfSupportedFeatures` ❌, `preferredSearch` ❌, `numNfInstComplete` ❌. `GET /scp-domain-routing-info` ❌. Неизвестные параметры игнорируются (допустимо по §6.2.3.2.3.1).

### 1.4 Nnrf_AccessToken (TS 29.510 §5.4, TS 33.501 §13.4)

| Операция | Статус |
|---|---|
| `POST /oauth2/token` (`AccessTokenReq` → `AccessTokenRsp`) | ❌ 🔴 |
| Проверка `3gpp-Sbi-Client-Credentials` (CCA) | ❌ |
| Модели `AccessTokenReq/Rsp/Err/Claims` | ✅ (только сгенерированы, `lib/sbi/openapi/model`) |

---

## 2. SCP (TS 23.501 Annex E, TS 29.500 §6.10)

| Требование | Статус | Комментарий | Файл |
|---|---|---|---|
| Model C: маршрутизация по `3gpp-Sbi-Target-apiRoot` | ✅ | | `src/scp/sbi-path.c:245,1225` |
| Model D: delegated discovery по `3gpp-Sbi-Discovery-*` | ✅ | поддерживаются: `target-nf-type`, `requester-nf-type`, `service-names`, `target/requester-nf-instance-id`, `snssais`, `dnn`, `tai`, `guami`, `target/requester-plmn-list`, `hnrf-uri`, `requester-features` | `sbi-path.c` |
| `3gpp-Sbi-Discovery-supi`, `-routing-indicator`, `-nf-set-id`, `-preferred-locality`, `-supported-features` | ❌ | пробрасывать нечего — NRF их не фильтрует | |
| Кэш результатов discovery, `validityPeriod` | ✅ | | `lib/sbi/context.c` |
| Выбор продюсера по `priority`/`capacity` | ⚠️ | priority ✅; capacity/load — не взвешивается | `lib/sbi/context.c:976` |
| Возврат `3gpp-Sbi-Producer-Id` | ✅ | | `sbi-path.c` |
| Проброс `3gpp-Sbi-Callback` (нотификации через SCP) | ✅ | | `sbi-path.c:247` |
| Next-hop SCP (`3gpp-Sbi-Nrf-Uri`, chained SCP) | ⚠️ | статический next-hop; `scpDomains`/`scp-domain-routing-info` ❌ | |
| Reselection при 5xx/timeout продюсера в пределах NF Set | ❌ | нет NF Set в профилях | |
| `3gpp-Sbi-Routing-Binding` учёт | ❌ | | |
| `3gpp-Sbi-Oci`/`Lci` учёт | ❌ | | |
| OAuth2: получение токена от имени консьюмера, проброс `Authorization` | ❌ 🔴 | `Authorization`, пришедший от консьюмера, пробрасывается как обычный заголовок | |
| Ошибки SCP (§6.10.8): `TARGET_NF_NOT_REACHABLE` (504), `NRF_NOT_REACHABLE` (500) | ⚠️ | ✅ эти два; `NF_DISCOVERY_FAILURE`, `NF_SELECTION_FAILURE`, `SCP_INTERNAL_ERROR` ❌; 16 из 23 ошибок без `cause` | `sbi-path.c` |
| Защита от петель (`Via`, hop count) | ❌ | | |
| Регистрация SCP в NRF с `scpInfo` (`scpDomainInfoList`, `scpPorts`) | ✅ | | `src/scp/context.c:71` |
| TLS клиент/сервер, mTLS | ✅ опционально | по умолчанию http | `configs/open5gs/scp.yaml.in:150` |
| HTTP/2 multipart (N1/N2 контент) прозрачно | ✅ | | `lib/sbi/message.c` |

---

## 3. AMF

### 3.1 N2 — NGAP (TS 38.413 §8)

| Процедура | Направление | Статус | Файл |
|---|---|---|---|
| NG Setup, NG Reset, Error Indication | ⇄ | ✅ | `src/amf/ngap-handler.c`, `ngap-build.c` |
| RAN Configuration Update | RAN→AMF | ✅ | |
| AMF Configuration Update | AMF→RAN | ⚠️ построитель есть (`ngap_build_amf_configuration_update`), обработка `AMFConfigurationUpdateAcknowledge/Failure` в `ngap-sm.c` отсутствует | |
| AMF Status Indication | AMF→RAN | ❌ 🟡 | |
| Overload Start / Overload Stop | AMF→RAN | ❌ 🟡 коммерческий gNB ожидает при перегрузке | |
| Initial UE Message, Uplink/Downlink NAS Transport | ⇄ | ✅ | |
| NAS Non Delivery Indication | RAN→AMF | ❌ 🟡 | |
| Reroute NAS Request | AMF→RAN | ❌ ⚪ (нужен для AMF re-allocation) | |
| Initial Context Setup | ⇄ | ✅ | |
| UE Context Release Request / Command / Complete | ⇄ | ✅ | |
| UE Context Modification | ⇄ | ✅ | |
| UE Radio Capability Info Indication | RAN→AMF | ✅ | |
| UE Radio Capability Check | ⇄ | ❌ ⚪ | |
| PDU Session Resource Setup / Modify / Release | ⇄ | ✅ | |
| PDU Session Resource Notify / Modify Indication | RAN→AMF | ❌ 🟡 (RAN-инициированная модификация, QoS notify) | |
| Handover Preparation (Required/Command/PreparationFailure) | ⇄ | ✅ | |
| Handover Resource Allocation (Request/RequestAck/Failure) | ⇄ | ✅ | |
| Handover Notification, Handover Cancel | ⇄ | ✅ | |
| Path Switch Request (Xn HO) | ⇄ | ✅ | |
| Uplink/Downlink RAN Status Transfer | ⇄ | ✅ | |
| Uplink/Downlink RAN Configuration Transfer | ⇄ | ✅ | |
| Paging | AMF→RAN | ✅ | `ngap_build_paging` |
| Location Reporting Control / Location Report | ⇄ | ❌ ⚪ | |
| Write-Replace Warning / PWS Cancel / PWS Restart/Failure | ⇄ | ❌ ⚪ | |
| Trace Start / Deactivate Trace / Cell Traffic Trace | ⇄ | ❌ ⚪ | |
| UE TNLA Binding Release, Downlink UE Associated NRPPa | | ❌ ⚪ | |
| Secondary RAT Data Usage Report | RAN→AMF | ❌ 🟡 (EN-DC/NR-DC учёт) | |
| SCTP multi-homing (RFC 9260), несколько AMF-адресов в NG Setup | | ✅ | `lib/sctp` |

### 3.2 N1 — 5GMM (TS 24.501 §5)

| Процедура | Статус | Комментарий |
|---|---|---|
| Registration: initial / mobility / periodic / emergency | ✅ | `src/amf/gmm-sm.c` |
| Registration Accept IE: TAI list, allowed/configured/rejected NSSAI, T3512, T3502, network feature support, PDU session status, LADN, MICO | ⚠️ | базовый набор; `SOR transparent container`, `NSSRG`, `NSAG`, `pending NSSAI` — кодек есть, логика ❌ |
| Authentication (5G-AKA) | ✅ | `RES*` проверка, `AUTS` re-sync через UDM |
| Authentication (EAP-AKA') | ❌ 🟡 | AUSF Open5GS умеет EAP-AKA' (`src/ausf/nudm-handler.c:25`), но AMF не переносит EAP в NAS |
| Security Mode Command / Complete / Reject | ✅ | NEA0-3, NIA0-3; IMEISV request; `selected EPS NAS algorithms` ❌ |
| Identity Request / Response (SUCI, IMEI, IMEISV) | ✅ | |
| Service Request / Accept / Reject | ✅ | |
| Deregistration UE-initiated (3GPP / non-3GPP / both) | ✅ | |
| Deregistration network-initiated | ✅ | `gmm_build_de_registration_request` |
| Configuration Update Command / Complete | ✅ | |
| UL/DL NAS Transport — payload `N1 SM information` | ✅ | |
| UL NAS Transport — `SMS`, `LPP`, `SOR`, `UE policy container`, `UPU`, `Location services`, `CIoT user data` | ❌ 🟡 | `gmm-handler.c` обрабатывает только `OGS_NAS_PAYLOAD_CONTAINER_N1_SM_INFORMATION` |
| Notification / Notification Response | ❌ ⚪ | non-3GPP paging |
| 5GMM Status | ✅ | `gmm_build_status` |
| Таймеры T3513, T3522, T3550, T3555, T3560, T3570 | ✅ | `src/amf/timer.h` |
| Mobile reachable, implicit deregistration | ✅ | |
| T3512 (periodic), T3502, T3346 (backoff) в Accept/Reject | ⚠️ | T3512/T3502 выдаются; T3346 ❌ |
| Network slicing: NSSF-based selection, rejected NSSAI causes | ✅ | |
| UE radio capability ID (RACS) | ❌ ⚪ | |
| N26 / EPS interworking (mapped EPS bearer contexts, EPS NAS container) | ❌ ⚪ | нет N26 (`grep N26` — 0) |
| SUCI: null / Profile A / Profile B от UE — прозрачно передаётся в AUSF/UDM | ✅ | |

### 3.3 Namf_Communication (TS 29.518 §5.2) — AMF как продюсер

| Операция | Статус | Файл |
|---|---|---|
| N1N2MessageTransfer (`POST /ue-contexts/{id}/n1-n2-messages`) | ✅ | `src/amf/namf-handler.c` |
| N1N2MessageSubscribe / Unsubscribe | ❌ 🟡 | |
| N1N2TransferFailureNotification (client) | ⚠️ | статусы `N1_N2_TRANSFER_INITIATED`, `ATTEMPTING_TO_REACH_UE` ✅; `UE_NOT_RESPONDING` ⚠️ |
| UEContextTransfer (`POST /ue-contexts/{id}/transfer`) | ✅ | |
| RegistrationStatusUpdate (`/transfer-update`) | ✅ | |
| ReleaseUEContext (`/release`) | ✅ | |
| CreateUEContext / RelocateUEContext / CancelRelocateUEContext (N2 HO между AMF) | ❌ ⚪ | |
| EBIAssignment (`/assign-ebi`) | ❌ ⚪ | нужен только для EPS interworking |
| AMFStatusChangeSubscribe / Unsubscribe / Notify | ❌ 🟡 | вендорские SMF/PCF могут подписываться при старте |
| NonUeN2MessageTransfer / NonUeN2InfoSubscribe (PWS, NRPPa) | ❌ ⚪ | |
| Namf_EventExposure (`/namf-evts/v1/subscriptions`) | ❌ 🟡 | вендорские PCF/NEF/CHF используют для location/reachability |
| Namf_MT (`/namf-mt/v1/ue-contexts/{id}/ue-reachind`, `enable-ue-reachability`) | ❌ 🟡 | нужен вендорскому SMSF/UDM для MT SMS |
| Namf_Location (`/namf-loc/v1`) | ❌ ⚪ | |

### 3.4 AMF как консьюмер

| Сервис | Операции | Статус | Комментарий |
|---|---|---|---|
| Nausf_UEAuthentication | `POST /ue-authentications`, `PUT .../5g-aka-confirmation` | ✅ | EAP-session ❌; `3gpp-Sbi-Discovery-routing-indicator` не передаётся 🔴 при нескольких вендорских AUSF |
| Nudm_UECM | `PUT amf-3gpp-access`, `PATCH` (purge), `dereg-notify` callback | ✅ | `amf-non-3gpp-access` ❌ |
| Nudm_SDM | `GET am-data`, `smf-select-data`, `ue-context-in-smf-data`, `nssai`; `POST/DELETE sdm-subscriptions`; `sdm-change-notify` callback | ✅ | `lcs-privacy`, `lcs-mo` ❌ ⚪ |
| Nnssf_NSSelection | `GET /network-slice-information` | ✅ | |
| Npcf_AMPolicyControl | `POST /policies`, `DELETE`, `policy-update-notification` callback | ✅ | `PATCH /policies/{id}/update` (trigger report) ❌ 🟡; **отказ PCF → отказ регистрации UE** 🔴 |
| Npcf_UEPolicyControl | | ❌ 🟡 | вендорский PCF с URSP ожидает |
| Nsmf_PDUSession | `POST sm-contexts` (multipart N1/N2), `POST .../modify`, `POST .../release`, `sm-context-status-notify` callback | ✅ | `retrieve` ❌; `3gpp-Sbi-Binding` от SMF игнорируется |
| Nnrf_NFManagement / NFDiscovery | | ✅ | через SCP (Model D) или напрямую |
| Nnrf_AccessToken | | ❌ 🔴 | AMF не отправляет `Authorization: Bearer` |
| Nsmsf_SMService | | ❌ 🟡 | |
| Nlmf_Location | | ❌ ⚪ | |

---

## 4. UDM

### 4.1 Nudm_UEAuthentication (TS 29.503 §5.4)

| Операция | Статус | Комментарий |
|---|---|---|
| `POST /{supiOrSuci}/security-information/generate-auth-data` | ✅ | `servingNetworkName` обязателен; `resynchronizationInfo` (AUTS) ✅ |
| SUCI deconcealment: null scheme, Profile A (Curve25519), Profile B (secp256r1), несколько `hnPublicKeyId` | ✅ | `configs/open5gs/udm.yaml.in:12 hnet`, `lib/crypt/ecc.c`, `curve25519` |
| Генерация AV: 5G-AKA (Milenage, ARPF внутри UDM) | ✅ | `authenticationMethod` только `5G_AKA` (`src/udm/nudr-handler.c`) |
| EAP-AKA' AV (`CK'/IK'`) | ❌ 🟡 | AUSF Open5GS готов, UDM не выдаёт |
| TUAK | ❌ ⚪ | |
| `POST /{supi}/auth-events` (AuthEvent) | ✅ | |
| `PUT /{supi}/auth-events/{authEventId}` (удаление) | ❌ ⚪ | |
| `POST /{supi}/hss-security-information/{hssAuthType}/generate-av` (EPS AKA / IMS AKA для 5G-HSS) | ❌ ⚪ | |
| `PUT /{supi}/gba-security-information/generate-av` | ❌ ⚪ | |
| Ответ 404 с `cause: USER_NOT_FOUND`, 403 `AUTHENTICATION_REJECTED`/`SERVING_NETWORK_NOT_AUTHORIZED` | ⚠️ | 404/403 есть, `cause` ❌ 🔴 |

### 4.2 Nudm_SDM (TS 29.503 §5.2)

| Ресурс | Статус | Комментарий |
|---|---|---|
| `GET /{supi}/am-data` | ✅ | `supported-features`, `plmn-id` params ⚠️ принимаются, не влияют |
| `GET /{supi}/smf-select-data` | ✅ | |
| `GET /{supi}/sm-data` (`single-nssai`, `dnn`) | ✅ | прокси из UDR, фильтрация по `single-nssai`/`dnn` ✅ |
| `GET /{supi}/nssai` | ✅ | |
| `GET /{supi}/ue-context-in-smf-data` | ✅ | |
| `GET /{supi}/ue-context-in-smsf-data`, `sms-mng-data`, `sms-data` | ❌ 🟡 | нужны вендорскому SMSF |
| `GET /{supi}/lcs-mo-data`, `lcs-privacy-data`, `lcs-bca-data` | ❌ ⚪ | |
| `GET /{supi}/trace-data` | ❌ ⚪ | |
| `GET /{supi}/v2x-data`, `prose-data`, `uc-data`, `mbs-data`, `a2x-data` | ❌ ⚪ | |
| `GET /{ueId}/id-translation-result` (GPSI→SUPI) | ❌ 🟡 | нужен NEF/SMSF |
| `GET /{supi}` (несколько dataset через `dataset-names`) | ✅ | лимит `MAX_NUM_OF_DATASETNAMES` |
| `POST /{ueId}/sdm-subscriptions`, `DELETE .../{id}` | ✅ | |
| `PATCH /{ueId}/sdm-subscriptions/{id}` | ❌ ⚪ | |
| `DataChangeNotification` (client) | ✅ | при изменении в UDR |
| `PUT /{supi}/am-data/sor-ack`, `upu-ack`, `cag-ack`, `snssais-ack` | ❌ ⚪ | SoR/UPU не реализованы |
| `GET /shared-data`, `POST /shared-data-subscriptions` | ❌ 🟡 | вендорские AMF/SMF часто запрашивают `sharedDataIds` |
| `GET /{supi}/ue-context-in-amf-data` | ❌ ⚪ | |

### 4.3 Nudm_UECM (TS 29.503 §5.3)

| Ресурс | Статус | Комментарий |
|---|---|---|
| `PUT /{ueId}/registrations/amf-3gpp-access` | ✅ | `deregCallbackUri` обязателен; `guami`, `ratType`, `pei` ✅; `imsVoPs`, `backupAmfInfo` ⚠️ |
| `PATCH /{ueId}/registrations/amf-3gpp-access` (purgeFlag, guami, pei) | ✅ | |
| `GET /{ueId}/registrations/amf-3gpp-access` | ❌ 🟡 | вендорский SMSF/NEF запрашивает serving AMF |
| `PUT/PATCH/GET /{ueId}/registrations/amf-non-3gpp-access` | ❌ ⚪ | |
| `PUT /{ueId}/registrations/smf-registrations/{pduSessionId}` | ✅ | |
| `DELETE /{ueId}/registrations/smf-registrations/{pduSessionId}` | ✅ | |
| `GET /{ueId}/registrations/smf-registrations` | ❌ 🟡 | |
| `PUT/DELETE /{ueId}/registrations/smsf-3gpp-access`, `smsf-non-3gpp-access` | ❌ 🟡 | нужен вендорскому SMSF |
| `GET /{ueId}/registrations` (все) | ❌ ⚪ | |
| `DeregistrationNotification` (client → AMF `dereg-notify`) | ✅ | при повторной регистрации в другом AMF |
| `PCscfRestorationNotification`, `DataRestorationNotification` | ❌ ⚪ | |
| `POST /restore-pcscf`, `/{ueId}/registrations/send-routing-info-sm` | ❌ ⚪ | |
| `roaming-info-update` | ❌ ⚪ | |

### 4.4 Прочие сервисы UDM

| Сервис | Статус | Нужен |
|---|---|---|
| Nudm_EE (event exposure) | ❌ | 🟡 вендорские NEF/PCF |
| Nudm_PP (parameter provision) | ❌ | ⚪ |
| Nudm_NIDDAU | ❌ | ⚪ |
| Nudm_MT (`/namf-mt` proxy, `ue-reachability`) | ❌ | 🟡 SMSF |
| Nudm_SSAU, Nudm_RSDS, Nudm_UECM (broadcast) | ❌ | ⚪ |

### 4.5 UDM как консьюмер Nudr_DataRepository (TS 29.504/29.505) — критично при вендорском UDR

| Операция к UDR | Статус | Требование к вендорскому UDR |
|---|---|---|
| `GET /subscription-data/{ueId}/authentication-data/authentication-subscription` | ✅ | `authenticationMethod`, `encPermanentKey`, `encOpcKey`/`encTopcKey`, `authenticationManagementField`, `sequenceNumber` в формате 29.505 |
| `PATCH .../authentication-subscription` (`/sequenceNumber` replace) | ✅ | UDR обязан поддерживать JSON Patch |
| `PUT /subscription-data/{ueId}/authentication-data/authentication-status` | ✅ | |
| `GET .../{servingPlmnId}/provisioned-data/am-data`, `smf-selection-subscription-data`, `sm-data` | ✅ | |
| `GET .../provisioned-data` (несколько dataset) | ✅ | |
| `PUT/PATCH /subscription-data/{ueId}/context-data/amf-3gpp-access` | ✅ | |
| `PUT/DELETE .../context-data/smf-registrations/{psi}` | ✅ | |
| `GET .../context-data/smf-registrations` | ✅ | |
| `POST/DELETE .../context-data/sdm-subscriptions` | ✅ | |
| `PATCH .../provisioned-data/am-data` (sor/upu ack) | ❌ | — |
| Подписка на изменения в UDR (`/subscription-data/subs-to-notify`) | ❌ | UDM не узнает об изменении подписки, если её меняет вендорская OSS/провижн |

---

## 5. Общее для SBI (TS 29.500, 29.501)

| Требование | Статус | Комментарий |
|---|---|---|
| HTTP/2 (RFC 9113), TLS 1.2/1.3 (RFC 8446), ALPN `h2` | ✅ | по умолчанию plaintext h2c prior-knowledge |
| mTLS, проверка SAN по FQDN | ✅ опционально | `default.tls`, `verify_client` |
| OAuth2 Bearer (consumer) / валидация (producer) | ❌ 🔴 | |
| `Content-Type: application/json`, `application/problem+json`, `multipart/related` (N1/N2, `3gpp-Sbi-*`) | ✅ | |
| `Accept-Encoding`/gzip | ❌ ⚪ | |
| API version в URI (`/v1`, `/v2`), `versions` в NFService | ✅ | Nudm `v2`, Nnssf `v2`, прочие `v1` |
| `supportedFeatures` negotiation (29.500 §6.6) | ⚠️ | принимается, почти нигде не влияет на поведение |
| `3gpp-Sbi-Message-Priority` | ✅ | парсится, приоритизация не реализована |
| `3gpp-Sbi-Callback` | ✅ | |
| `3gpp-Sbi-Target-apiRoot`, `Producer-Id`, `Target-Nf-Id`, `Discovery-*` | ✅ | |
| `3gpp-Sbi-Binding`, `Routing-Binding` | ❌ 🟡 | |
| `3gpp-Sbi-Oci`, `Lci` | ❌ 🟡 | |
| `3gpp-Sbi-Correlation-Info` | ❌ 🟡 | |
| `3gpp-Sbi-Client-Credentials` | ❌ 🔴 | |
| `3gpp-Sbi-Sender-Timestamp`, `Max-Rsp-Time` | ❌ ⚪ | |
| `3gpp-Sbi-Response-Info`, `Access-Scope`, `Access-Token` | ❌ ⚪ | |
| `3gpp-Sbi-Etags` | ✅ | |
| ProblemDetails: `type`, `title`, `status`, `detail`, `instance` | ⚠️ | `type` ✅, `title`/`detail` ✅, **`cause` в 6% ответов** 🔴, `invalidParams` ❌ |
| Коды 400/403/404/500/503/504 | ✅ | 429 (overload), 308 (redirect на другой NF Set) ❌ |
| Retry / `Retry-After` | ❌ 🟡 | |
| JSON Patch (RFC 6902) в PATCH | ⚠️ | только `replace` |
| Идемпотентность PUT/DELETE | ✅ | |

---

## 6. Итог по четырём NF для вашей топологии

| NF | Готовность к вендорскому окружению | Блокеры (🔴) | Существенные (🟡) |
|---|---|---|---|
| **NRF** | Низкая | потеря `*Info`/`nfSetIdList` в профилях; нет `/oauth2/token`; нет фильтра `supi`/`routing-indicator`; `cause` в ошибках | subscrCond 3/11; `NF_PROFILE_CHANGED`; PATCH только replace; bootstrapping |
| **SCP** | Средняя | нет получения токенов от имени консьюмера; `cause` в ошибках | нет reselection/Binding/Oci-Lci; нет scpDomains; нет loop-protection |
| **AMF** | Средняя | нет Bearer в запросах; `routing-indicator` при discovery AUSF/UDM; отказ PCF = отказ регистрации; `cause` в ошибках | Namf_EventExposure, AMFStatusChange, Namf_MT; UL NAS только SM; Overload/AMFStatus NGAP; EAP-AKA'; Npcf_UEPolicyControl |
| **UDM** | Средняя | нет Bearer к UDR; `cause` в ошибках (114/120) | Nudm_EE, shared-data, GET registrations, smsf-registrations, EAP-AKA' AV, sms-data |

**Порядок исправления, вытекающий из матрицы:** (1) NRF-хранение профилей «как есть» + фильтры `supi`/`routing-indicator`/`nf-set-id` → (2) `cause` во всех ProblemDetails → (3) OAuth2 (NRF + SCP + консьюмерская часть AMF/UDM) → (4) TLS по умолчанию → (5) Binding/Oci/Lci → (6) 🟡-сервисы по потребности вендоров.
