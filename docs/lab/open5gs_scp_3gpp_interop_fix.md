# Open5GS SCP — 3GPP-compliant routing/interoperability fix

## 1. Цель

Исправить SCP в форке Open5GS так, чтобы он корректно работал как Service Communication Proxy в лаборатории с внешними коммерческими NF (в первую очередь Nexign CHF, далее PCF/SMF), без vendor-specific костылей.

Основная задача:

- корректно обрабатывать indirect communication через SCP;
- корректно маршрутизировать `Nnrf_NFManagement` в NRF;
- не отправлять transit SBI requests в локальный SCP handler, если next-hop не определён;
- корректно работать при частично заполненных discovery headers, когда service однозначно определяется из SBI URI;
- возвращать корректные HTTP status / ProblemDetails;
- сохранить совместимость с текущими рабочими Open5GS NF;
- добавить regression tests.

Это должно быть generic-решение для SBI routing, а не отдельное исключение вида `if vendor == Nexign`.

---

# 2. Текущая архитектура лаборатории

Используется Open5GS SCP.

NRF доступен по адресу:

```text
http://172.16.7.100:18491
```

SCP уже умеет успешно форвардить часть `Nnrf_NFManagement` трафика в NRF.

Пример успешно работающего PATCH:

```text
[sbi] DEBUG: [PATCH] /nnrf-nfm/v1/nf-instances/aa456c20-ab99-41f1-941b-99578b3d6ad2
[sbi] DEBUG: [{"op":"replace","path":"/nfStatus","value":"REGISTERED"},{"op":"replace","path":"/load","value":0}]

[sbi] DEBUG: [PATCH] http://172.16.7.100:18491/nnrf-nfm/v1/nf-instances/aa456c20-ab99-41f1-941b-99578b3d6ad2

[sbi] DEBUG: [204:PATCH] http://172.16.7.100:18491/nnrf-nfm/v1/nf-instances/aa456c20-ab99-41f1-941b-99578b3d6ad2
```

Это доказывает, что:

- SCP -> NRF connectivity работает;
- HTTP/2 client работает;
- forwarding в NRF уже реализован;
- NRF отвечает корректно;
- проблема находится именно в routing decision для определённых входящих SBI requests.

---

# 3. Проблемный сценарий

Внешний Nexign CHF выполняет регистрацию через SCP:

```http
PUT /nnrf-nfm/v1/nf-instances/97986d02-4c4d-480d-b0e3-87de526bda20
```

NF Profile:

```json
{
  "nfInstanceId": "97986d02-4c4d-480d-b0e3-87de526bda20",
  "nfType": "CHF",
  "nfStatus": "REGISTERED",
  "heartBeatTimer": 10,
  "chfInfo": {
    "groupId": "nx-chf"
  },
  "nfServiceList": {
    "CHF-charging": {
      "serviceName": "nchf-convergedcharging",
      "scheme": "http",
      "nfServiceStatus": "REGISTERED",
      "ipEndPoints": [
        {
          "ipv4Address": "172.16.74.1",
          "transport": "TCP",
          "port": 29643
        }
      ],
      "vendorId": "nexign"
    },
    "CHF-subscription": {
      "serviceName": "nchf-spendinglimitcontrol",
      "scheme": "http",
      "nfServiceStatus": "REGISTERED",
      "ipEndPoints": [
        {
          "ipv4Address": "172.16.74.1",
          "transport": "TCP",
          "port": 29643
        }
      ],
      "vendorId": "nexign"
    }
  }
}
```

---

# 4. Наблюдаемая ошибка №1

В одной версии/ветке текущего runtime Open5GS:

```text
[scp] ERROR: [/nnrf-nfm/v1/nf-instances/97986d02-4c4d-480d-b0e3-87de526bda20] No Mandatory Discovery [1:0]

[sbi] WARNING: server callback error
[sbi] DEBUG: STATUS [500]
```

`[1:0]` означает:

```text
target_nf_type = NRF
service_name   = NULL
```

То есть SCP распознал:

```text
3gpp-Sbi-Discovery-target-nf-type: NRF
```

но не получил или не распознал:

```text
3gpp-Sbi-Discovery-service-names: nnrf-nfm
```

При этом service однозначно определяется из URI:

```text
/nnrf-nfm/v1/...
```

---

# 5. Наблюдаемая ошибка №2

В другом случае тот же тип запроса доходит до локального SCP state machine:

```text
[PUT] /nnrf-nfm/v1/nf-instances/97986d02-4c4d-480d-b0e3-87de526bda20

[scp] ERROR: Invalid resource name [nf-instances]

[sbi] DEBUG: STATUS [400]

{
  "type": "/nnrf-nfm/v1",
  "title": "Invalid resource name",
  "status": 400,
  "detail": "nf-instances",
  "instance": "/nf-instances/97986d02-4c4d-480d-b0e3-87de526bda20"
}
```

Это означает:

1. SCP не выбрал NRF как next-hop.
2. Request не был forwarded.
3. Request провалился в `scp_state_operational()`.
4. SCP попытался обработать `nnrf-nfm/nf-instances` как API самого SCP.
5. Локальный handler SCP ожидает другие ресурсы, например `nf-status-notify`.
6. `nf-instances` считается invalid resource.
7. Возвращается HTTP 400.

Это неправильное поведение для transit SBI request.

---

# 6. Главная проблема

Текущий routing pipeline смешивает два принципиально разных класса запросов:

## 6.1. Local SCP request

Например notification, реально адресованный SCP:

```text
/nnrf-nfm/v1/nf-status-notify/...
```

Такой запрос должен обрабатываться локальной SCP state machine.

## 6.2. Transit request

Например:

```text
PUT /nnrf-nfm/v1/nf-instances/{nfInstanceId}
```

с target NF = NRF.

Такой request должен быть forwarded в NRF.

Если SCP не смог выбрать next-hop, transit request нельзя автоматически передавать в локальный `scp_state_operational()`.

---

# 7. Требуемое целевое поведение

Для проблемного Nexign CHF должно происходить:

```text
Nexign CHF
    |
    | PUT /nnrf-nfm/v1/nf-instances/<CHF-ID>
    |
    | target-nf-type = NRF
    | service-names отсутствует
    v
Open5GS SCP
    |
    | infer service from URI
    | /nnrf-nfm/ -> nnrf-nfm
    |
    | target NF = NRF
    v
configured NRF client
    |
    | PUT /nnrf-nfm/v1/nf-instances/<CHF-ID>
    v
NRF 172.16.7.100:18491
    |
    | 200/201/204 according to operation
    v
Open5GS SCP
    |
    v
Nexign CHF
```

---

# 8. Принцип реализации

Не делать:

```c
if (vendor == NEXIGN)
    ...
```

Не делать исключение только для CHF.

Не делать hardcoded exception только для конкретного UUID.

Нужно исправить generic SBI routing.

---

# 9. P0.1 — Service inference из SBI URI

## Проблема

Если есть:

```text
target-nf-type = NRF
```

но отсутствует:

```text
service-names
```

Open5GS сейчас отвергает request.

Однако service может быть однозначно определён из SBI URI:

```text
/nnrf-nfm/v1/... -> nnrf-nfm
/nudm-sdm/v2/... -> nudm-sdm
/nudm-ueau/v1/... -> nudm-ueau
/npcf-smpolicycontrol/v1/... -> npcf-smpolicycontrol
/nsmf-pdusession/v1/... -> nsmf-pdusession
/nchf-convergedcharging/v3/... -> nchf-convergedcharging
```

## Требование

Если:

```text
target_nf_type_presence == true
service_names_presence == false
```

то до reject необходимо попытаться определить service name из URI.

Псевдокод:

```c
if (target_nf_type_presence && !service_names_presence) {

    service_name = ogs_sbi_service_name_from_request_uri(request);

    if (service_name != OpenAPI_service_name_NULL) {

        service_names_presence = true;

        ogs_warn(
            "Missing 3gpp-Sbi-Discovery-service-names; "
            "inferred service [%s] from URI [%s]",
            OpenAPI_service_name_ToString(service_name),
            request->h.uri);
    }
}
```

ВАЖНО:

Не создавать вручную огромный список `strncmp()` для всех 3GPP service names, если в Open5GS уже есть parser/service registry.

Нужно сначала найти существующий механизм:

- URI parser;
- API name parser;
- service name enum conversion;
- service registry;
- `ogs_sbi_parse_request()`;
- `ogs_sbi_service_name_*`;
- аналогичный код в NRF/SCP/SEPP.

Использовать существующий механизм Open5GS.

---

# 10. P0.2 — Validation после inference

После попытки inference выполнить проверку.

Логика:

```c
if (target_nf_type_presence || service_names_presence) {

    if (!target_nf_type_presence || !service_names_presence) {

        ogs_error(
            "Incomplete discovery information [%d:%d] URI [%s]",
            target_nf_type,
            service_name,
            request->h.uri);

        ogs_sbi_server_send_error(
            stream,
            OGS_SBI_HTTP_STATUS_BAD_REQUEST,
            NULL,
            "Incomplete discovery information",
            request->h.uri,
            NULL);

        return false;
    }
}
```

Не возвращать generic:

```text
server callback error
500
```

для client-side malformed/incomplete routing metadata.

В таком случае должен быть 4xx ProblemDetails.

---

# 11. P0.3 — Direct routing для NRF management

Если после parsing/inference получено:

```text
target_nf_type == NRF
service_name == nnrf-nfm
```

то SCP не должен выполнять discovery NRF через NRF.

Нужно использовать configured NRF client.

Целевой алгоритм:

```c
if (target_nf_type == OpenAPI_nf_type_NRF &&
    service_name == OpenAPI_service_name_nnrf_nfm) {

    client = NF_INSTANCE_CLIENT(ogs_sbi_self()->nrf_instance);

    if (!client) {
        ogs_error("NRF client not configured");

        send ProblemDetails:
            HTTP 503 Service Unavailable
            cause/detail = NRF unavailable / NRF client not configured

        return false;
    }

    forward_to_client(client, request);
    return true;
}
```

Нужно использовать уже существующую структуру Open5GS NRF instance/client, а не создавать новый HTTP client вручную.

---

# 12. P0.4 — Не отправлять transit requests в local SCP state machine

Это ключевая архитектурная правка.

Текущее нежелательное поведение:

```text
routing failed
     |
     v
scp_state_operational()
     |
     v
Invalid resource name
```

Нужно разделить:

```text
local_request
transit_request
```

## Local request

Request действительно адресован SCP:

```text
nf-status-notify
```

или другой API, который SCP обязан обрабатывать сам.

Тогда:

```text
-> scp_state_operational()
```

## Transit request

Request адресован другой NF.

Если next-hop не определён:

```text
НЕ -> scp_state_operational()
```

Вместо этого вернуть корректный ProblemDetails.

Например:

```text
HTTP 400
```

если request содержит некорректные/missing routing parameters.

Либо:

```text
HTTP 503
```

если routing корректен, но target NF/NRF недоступен.

---

# 13. P0.5 — Корректная классификация ошибок

Не использовать `500 Internal Server Error` для ожидаемых routing/validation ситуаций.

## 400 Bad Request

Применять для:

- malformed discovery header;
- invalid NF type;
- invalid service name;
- incomplete discovery information, которую невозможно восстановить;
- inconsistent routing information;
- invalid Target-apiRoot.

## 404

Только если семантически действительно resource/service не найден на локальном API или это соответствует конкретной 3GPP procedure.

Не использовать 404 просто потому, что SCP не смог найти producer.

## 503 Service Unavailable

Применять когда:

- configured NRF отсутствует;
- NRF client недоступен;
- target NF найден, но transport unavailable;
- discovery временно невозможно выполнить из-за недоступности NRF.

## 500 Internal Server Error

Только для действительно внутренней ошибки/инварианта:

- internal state corruption;
- unexpected NULL;
- allocation failure;
- impossible state;
- internal callback failure, не вызванный malformed request.

---

# 14. P0.6 — Не ломать `3gpp-Sbi-Target-apiRoot`

Если request содержит:

```text
3gpp-Sbi-Target-apiRoot
```

он должен иметь более высокий routing priority, чем delegated discovery.

Целевой порядок определения next-hop:

```text
1. Explicit 3gpp-Sbi-Target-apiRoot
2. Existing binding / selected producer / existing transaction context
3. Explicit target NF + service discovery parameters
4. NRF special case
5. Local SCP API
6. Error
```

Не выполнять discovery, если Target-apiRoot уже однозначно задаёт destination.

---

# 15. P0.7 — Проверить parsing `service-names`

Нужно проверить поддержку:

```text
3gpp-Sbi-Discovery-service-names
```

в том числе:

- один service;
- несколько services;
- case handling;
- percent encoding;
- whitespace;
- malformed values.

Не предполагать, что всегда будет только один service.

Для routing можно выбрать service, соответствующий service из URI.

Если headers содержат service, противоречащий URI:

```text
URI = /nnrf-nfm/...
service-names = nudm-sdm
```

необходимо вернуть 400, а не silently route request.

---

# 16. P0.8 — URI/service consistency validation

После parsing:

```text
service_from_uri
service_from_discovery
```

если оба присутствуют:

```c
if (service_from_uri != OpenAPI_service_name_NULL &&
    service_from_discovery != OpenAPI_service_name_NULL &&
    service_from_uri != service_from_discovery) {

    return 400;
}
```

ProblemDetails:

```json
{
  "status": 400,
  "title": "Inconsistent SBI routing information",
  "detail": "Service name in discovery header does not match request URI"
}
```

---

# 17. P0.9 — Logging

Добавить нормальные routing logs.

Пример:

```text
[SCP] Routing request:
  method=PUT
  uri=/nnrf-nfm/v1/nf-instances/<id>
  target_nf_type=NRF
  service_header=NULL
  service_uri=nnrf-nfm
  target_api_root=NULL
  routing_mode=delegated
```

После inference:

```text
[SCP] Service inferred from URI:
  service=nnrf-nfm
```

После выбора destination:

```text
[SCP] Route selected:
  target_nf_type=NRF
  service=nnrf-nfm
  destination=http://172.16.7.100:18491
  source=configured-nrf
```

При ошибке:

```text
[SCP] Routing failed:
  reason=incomplete-discovery-information
  target_nf_type=NRF
  service=NULL
```

НЕ логировать sensitive authentication data.

---

# 18. P1 — Обобщить service inference

Нужно сделать generic helper.

Предлагаемая сигнатура:

```c
OpenAPI_service_name_e
ogs_sbi_service_name_from_uri(const char *uri);
```

или использовать уже существующую функцию Open5GS, если она есть.

Функция должна извлекать первый path component:

```text
/nnrf-nfm/v1/...              -> nnrf-nfm
/nudm-sdm/v2/...              -> nudm-sdm
/nsmf-pdusession/v1/...       -> nsmf-pdusession
/npcf-am-policy-control/v1/...-> npcf-am-policy-control
```

Затем использовать существующий enum converter Open5GS.

Не использовать vendor-specific таблицы.

---

# 19. P1 — Защитить local SCP handler

В `src/scp/scp-sm.c` добавить явную проверку, что запрос действительно локальный.

Не позволять transit `nf-instances` попадать в:

```c
case OpenAPI_service_name_nnrf_nfm:
```

и завершаться:

```text
Invalid resource name [nf-instances]
```

Если `nf-instances` неожиданно дошёл до local handler, это должно считаться routing-layer bug.

Например debug/assert только в debug build:

```c
ogs_error(
    "Transit Nnrf_NFManagement request reached local SCP handler [%s]",
    message.h.resource.component[0]);
```

В production всё равно вернуть корректный ProblemDetails, но лог должен явно указывать на routing issue.

---

# 20. P1 — Сохранить working flow

Нельзя сломать уже рабочий flow:

```text
PATCH /nnrf-nfm/v1/nf-instances/<id>
    |
    v
SCP
    |
    v
http://172.16.7.100:18491
    |
    v
204
```

Regression tests должны подтверждать сохранение этого поведения.

---

# 21. Regression tests — обязательный набор

Добавить automated tests для SCP routing.

## Test 1

```text
target_nf_type = NRF
service = nnrf-nfm
URI = /nnrf-nfm/v1/nf-instances/<id>
```

Ожидается:

```text
forward -> NRF
```

---

## Test 2 — основной Nexign interop case

```text
target_nf_type = NRF
service_names missing
URI = /nnrf-nfm/v1/nf-instances/<id>
```

Ожидается:

```text
service inferred as nnrf-nfm
forward -> configured NRF
```

Не должно быть:

```text
No Mandatory Discovery
Invalid resource name
500
```

---

## Test 3

```text
target_nf_type = UDM
service missing
URI = /nudm-sdm/v2/...
```

Ожидается:

```text
service inferred as nudm-sdm
normal discovery/routing
```

---

## Test 4

```text
target_nf_type missing
service = nnrf-nfm
```

Если target невозможно восстановить однозначно:

```text
400 Bad Request
```

---

## Test 5

```text
target_nf_type = NRF
service missing
URI = /unknown-service/v1/...
```

Ожидается:

```text
400 Bad Request
```

Не:

```text
500
```

---

## Test 6

```text
3gpp-Sbi-Target-apiRoot = http://172.16.7.100:18491
```

Ожидается:

```text
forward directly
```

Без NRF discovery.

---

## Test 7

```text
URI = /nnrf-nfm/v1/...
service_names = nudm-sdm
```

Ожидается:

```text
400 Bad Request
```

Причина:

```text
service header does not match URI
```

---

## Test 8

NRF configured, но TCP connection unavailable.

Ожидается:

```text
503 Service Unavailable
```

Не 400 и не generic 500.

---

## Test 9

Локальный notification:

```text
/nnrf-nfm/v1/nf-status-notify/...
```

Ожидается:

```text
local SCP processing
```

---

## Test 10

Transit request:

```text
/nnrf-nfm/v1/nf-instances/<id>
```

Никогда не должен попадать в local handler.

---

## Test 11

PATCH heartbeat:

```text
PATCH /nnrf-nfm/v1/nf-instances/<id>
```

Ожидается:

```text
forward NRF
204 propagated back
```

---

## Test 12

CHF registration:

```text
PUT /nnrf-nfm/v1/nf-instances/<CHF-ID>
```

Body содержит:

```text
nfType = CHF
```

Ожидается:

```text
NRF receives unchanged NF Profile
NRF response propagated to CHF
```

---

# 22. Integration test с реальным Nexign CHF

После unit/regression tests повторить реальный сценарий.

Ожидаемый SCP log:

```text
[PUT] /nnrf-nfm/v1/nf-instances/97986d02-...
[SCP] target NF type = NRF
[SCP] service name missing
[SCP] inferred service = nnrf-nfm
[SCP] route selected = configured NRF
[PUT] http://172.16.7.100:18491/nnrf-nfm/v1/nf-instances/97986d02-...
[201:PUT] ...
```

После этого через ~10 секунд CHF должен перейти на heartbeat/update:

```text
PATCH /nnrf-nfm/v1/nf-instances/97986d02-...
```

и получать:

```text
204
```

---

# 23. Проверка NRF

После успешной регистрации убедиться, что NRF действительно содержит CHF:

```text
nfInstanceId = 97986d02-4c4d-480d-b0e3-87de526bda20
nfType = CHF
nfStatus = REGISTERED
```

Должны присутствовать services:

```text
nchf-convergedcharging
nchf-spendinglimitcontrol
```

Endpoint:

```text
172.16.74.1:29643
```

---

# 24. Проверка discovery CHF

После регистрации выполнить discovery CHF через SCP/NRF.

Проверить, что consumer может получить CHF по:

```text
target-nf-type = CHF
service = nchf-convergedcharging
```

и получить endpoint:

```text
172.16.74.1:29643
```

---

# 25. Проверка следующего этапа — charging

После успешного registration/discovery проверить реальный SBI flow:

```text
SMF
 |
 | Nchf_ConvergedCharging
 v
SCP
 |
 | delegated discovery / producer selection
 v
CHF
```

Убедиться, что request/response body не изменяются SCP без необходимости.

---

# 26. Файлы, которые нужно проверить в первую очередь

Обязательно изучить:

```text
src/scp/sbi-path.c
src/scp/scp-sm.c
src/scp/sbi-path.h
src/scp/context.c
lib/sbi/
lib/sbi/client.c
lib/sbi/server.c
lib/sbi/message.c
lib/sbi/context.c
```

Также найти все места:

```bash
grep -R "No Mandatory Discovery" -n .
grep -R "Invalid resource name" -n src/scp
grep -R "target_nf_type_presence" -n src lib
grep -R "service_names_presence" -n src lib
grep -R "Target-apiRoot" -n src lib
grep -R "TARGET_APIROOT" -n src lib
grep -R "service_name" -n lib/sbi src/scp
```

---

# 27. Сначала проверить существующие helper-функции

Перед написанием нового parser найти, нет ли уже функций для:

- URI -> service name;
- service name string -> enum;
- service name enum -> string;
- parsing API version;
- parsing SBI path components.

Не дублировать функциональность.

---

# 28. Обязательно сравнить текущий source и запущенный binary

Запущенный процесс:

```text
/bin/open5gs-scpd -c /etc/open5gs/scp.yaml
```

Работает давно.

Есть признаки, что runtime binary может отличаться от текущего GitHub source.

Нужно определить:

```bash
/bin/open5gs-scpd -v
```

если поддерживается.

Также:

```bash
sha256sum /bin/open5gs-scpd
```

и определить commit/build metadata.

Если возможно, добавить в binary startup log:

```text
Open5GS version
git commit
build date
```

Чтобы дальше не возникало ситуации, когда анализируется GitHub `main`, а работает старый binary.

---

# 29. Не менять protocol semantics без необходимости

SCP не должен:

- менять NF Profile;
- менять NF Instance ID;
- переписывать CHF service endpoints;
- менять HTTP method;
- менять resource URI;
- подменять `nfType`;
- добавлять vendor-specific SBI fields;
- модифицировать JSON body без необходимости.

SCP здесь выполняет routing/forwarding.

---

# 30. Не делать fallback слишком permissive

Service inference разрешать только если service можно определить однозначно.

Хорошо:

```text
/nnrf-nfm/v1/... -> nnrf-nfm
```

Плохо:

```text
/foo/... -> guessing
```

Если URI неизвестен:

```text
400
```

---

# 31. Security / robustness

При parsing URI обязательно проверить:

- NULL;
- пустую строку;
- path traversal;
- слишком длинный URI;
- malformed percent encoding;
- repeated `/`;
- query parameters;
- fragment не должен использоваться;
- unexpected casing;
- invalid version component.

Нельзя допускать buffer overflow или чтение за пределами строки.

---

# 32. Memory management

Все новые allocations должны иметь симметричный free.

Желательно вообще не делать allocation при service inference — использовать существующий parsed request structure.

Проверить:

- leaks в error path;
- client refs;
- stream refs;
- transaction context;
- request cloning/free.

Это особенно важно, потому что long-running SCP process уже имеет высокий RSS.

---

# 33. Acceptance criteria

Изменение считается готовым, если выполнено всё ниже.

### A. Build

```text
meson setup / ninja
```

проходит без warning/error, связанных с патчем.

### B. Existing Open5GS tests

Не ломаются существующие tests.

### C. New SCP routing tests

Все новые tests проходят.

### D. Nexign CHF

Следующий request:

```text
PUT /nnrf-nfm/v1/nf-instances/97986d02-4c4d-480d-b0e3-87de526bda20
```

успешно forwarded на:

```text
http://172.16.7.100:18491
```

### E. Нет старых ошибок

Не должно быть:

```text
No Mandatory Discovery [1:0]
```

для однозначного URI.

Не должно быть:

```text
Invalid resource name [nf-instances]
```

для transit registration request.

Не должно быть generic:

```text
server callback error / 500
```

для recoverable validation/routing errors.

### F. NRF содержит CHF

NRF после registration видит:

```text
NF type: CHF
status: REGISTERED
```

### G. Heartbeat работает

Последующие PATCH получают 204.

### H. Existing Open5GS NF не сломаны

Текущие AMF/AUSF/UDM/UDR/PCF/SMF flows продолжают работать.

---

# 34. Definition of Done

Работа завершена, когда:

1. Найдена точная причина current routing failure.
2. Добавлен generic service inference.
3. `target=NRF + nnrf-nfm` корректно направляется в configured NRF.
4. Transit request больше не попадает в local SCP handler.
5. Error handling приведён к корректным 4xx/5xx semantics.
6. Сохранён `3gpp-Sbi-Target-apiRoot`.
7. Добавлены regression tests.
8. Nexign CHF успешно регистрируется.
9. CHF heartbeat проходит.
10. CHF discovery проходит.
11. Existing Open5GS flows не регрессировали.
12. Код не содержит Nexign-specific условий.
13. Изменения оформлены отдельными понятными commits.

---

# 35. Рекомендуемое разбиение commits

## Commit 1

```text
scp: add generic SBI service inference from request URI
```

## Commit 2

```text
scp: route Nnrf_NFManagement requests to configured NRF
```

## Commit 3

```text
scp: separate local and transit SBI request handling
```

## Commit 4

```text
scp: improve ProblemDetails and HTTP status handling
```

## Commit 5

```text
tests: add SCP delegated routing and NRF management regression tests
```

---

# 36. Важно для Cursor

Не ограничиваться механическим исправлением строки, где возникает:

```text
No Mandatory Discovery
```

Перед изменением кода полностью проследить request flow:

```text
nghttp2 server
    ->
SBI request parser
    ->
SCP request handler
    ->
routing decision
    ->
target selection / discovery
    ->
client selection
    ->
forwarding
    ->
response propagation
```

Нужно понять, почему один `PATCH /nnrf-nfm/...` уже успешно forwarding в NRF, а `PUT /nnrf-nfm/...` от внешнего CHF не проходит.

Использовать существующую архитектуру Open5GS, а не создавать параллельный routing implementation.

---

# 37. Итоговое ожидаемое поведение

До исправления:

```text
Nexign CHF
    |
    | PUT /nnrf-nfm/v1/nf-instances/<id>
    v
Open5GS SCP
    |
    +--> No Mandatory Discovery [NRF:NULL]
    |
    OR
    |
    +--> local SCP handler
            |
            +--> Invalid resource name [nf-instances]
                    |
                    +--> 400
```

После исправления:

```text
Nexign CHF
    |
    | PUT /nnrf-nfm/v1/nf-instances/<id>
    | target-nf-type = NRF
    | service-name may be absent
    v
Open5GS SCP
    |
    | parse URI
    | infer nnrf-nfm
    | select configured NRF
    v
Open5GS NRF
172.16.7.100:18491
    |
    | successful NF registration
    v
Open5GS SCP
    |
    v
Nexign CHF
```

Главный принцип:

> Исправить generic 3GPP SBI routing/interoperability в SCP, а не добавить workaround под конкретный CHF.
