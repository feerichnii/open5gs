# Open5GS NRF — приведение поведения к 3GPP TS 29.510 / TS 29.500

## 1. Цель

Исправить NRF в форке Open5GS так, чтобы обработка NF Registration / NF Update / NF Deregistration и NFProfile соответствовала 3GPP и была устойчивой к некорректным или частично совместимым внешним Network Functions.

Основные нормативные документы:

- 3GPP TS 29.510 / ETSI TS 129 510 — Network Function Repository Services
  - основной baseline: Release 19, v19.6.0;
  - сохранить interoperability с Release 18;
- 3GPP TS 29.500 / ETSI TS 129 500 — Service Based Architecture, Stage 3;
- 3GPP TS 29.501 — SBI principles / API versioning, где применимо;
- OpenAPI definitions Open5GS, сгенерированные из соответствующих 3GPP YAML schemas.

Главный принцип:

> Любой некорректный входной HTTP/SBI request должен приводить к корректному HTTP response + ProblemDetails. Входные данные от удалённой NF никогда не должны приводить к `assert()`, `abort()`, core dump или остановке NRF.

---

# 2. Реальный проблемный кейс

Внешний CHF выполняет:

```http
PUT /nnrf-nfm/v1/nf-instances/97986d02-4c4d-480d-b0e3-87de526bda20
```

Пример NFProfile:

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
      "serviceInstanceId": "adbc99a1-ea87-4b98-ab36-7813f8d0f8df",
      "serviceName": "nchf-convergedcharging",
      "versions": [
        {
          "apiVersionInUri": "v1",
          "apiFullVersion": "",
          "expiry": "2999-12-31T00:00:00"
        }
      ],
      "scheme": "http",
      "nfServiceStatus": "REGISTERED",
      "ipEndPoints": [
        {
          "ipv4Address": "172.16.74.1",
          "transport": "TCP",
          "port": 29643
        }
      ],
      "priority": 0,
      "capacity": 0,
      "load": 0,
      "vendorId": "nexign"
    },

    "CHF-subscription": {
      "serviceInstanceId": "adbc99a1-ea87-4b98-ab36-7813f8d0f8df",
      "serviceName": "nchf-spendinglimitcontrol",
      "versions": [
        {
          "apiVersionInUri": "v1",
          "apiFullVersion": "",
          "expiry": "2999-12-31T00:00:00"
        }
      ],
      "scheme": "http",
      "nfServiceStatus": "REGISTERED",
      "ipEndPoints": [
        {
          "ipv4Address": "172.16.74.1",
          "transport": "TCP",
          "port": 29643
        }
      ],
      "priority": 0,
      "capacity": 0,
      "load": 0,
      "vendorId": "nexign"
    }
  }
}
```

---

# 3. Текущий crash NRF

Наблюдаемый log:

```text
[nrf] DEBUG: nrf_state_operational(): OGS_EVENT_NAME_SBI_SERVER
[sbi] DEBUG: [NULL] NFInstance added with Ref [(null)]

[nrf] DEBUG: nrf_nf_state_initial(): ENTRY
[nrf] DEBUG: nrf_nf_state_will_register(): ENTRY
[nrf] DEBUG: nrf_nf_state_will_register(): OGS_EVENT_NAME_SBI_SERVER

[sock] DEBUG: addr:172.16.74.1, port:29643
[sock] DEBUG: addr:172.16.74.1, port:29643

[sbi] FATAL: ogs_sbi_client_associate:
Assertion `client' failed. (../lib/sbi/context.c:2392)

[core] FATAL: backtrace() returned ...
```

Это недопустимое поведение для NRF.

Удалённая NF управляет содержимым HTTP request. Следовательно, никакой field/combination fields из NFProfile не должен быть способен инициировать fatal assertion.

---

# 4. Нормативное поведение NFRegister

NF Registration:

```text
PUT /nnrf-nfm/{apiVersion}/nf-instances/{nfInstanceId}
```

Request body:

```text
NFProfile
```

При успешном создании новой регистрации NRF должен вернуть:

```text
201 Created
```

с representation зарегистрированного NFProfile и `Location` согласно процедуре NFRegister.

Если регистрация не может быть выполнена из-за ошибки request / NFProfile:

```text
400 Bad Request
Content-Type: application/problem+json
ProblemDetails
```

Если произошла реальная внутренняя ошибка NRF:

```text
500 Internal Server Error
ProblemDetails
```

Критически важно:

```text
invalid remote input != NRF internal error
```

Некорректный NFProfile — это client/request error, а не основание завершать процесс.

---

# 5. Основное правило robustness

Запрещено использовать следующие конструкции для проверки данных, пришедших по SBI:

```c
ogs_assert(pointer);
ogs_assert(valid_remote_field);
ogs_assert(client);
ogs_assert(service);
ogs_assert(address);
```

если значение зависит прямо или косвенно от request.

`assert()` допустим только для внутренних invariant, которые не могут быть нарушены удалённым request после полной validation.

Нужно заменить pattern:

```c
client = something_from_remote_profile(...);
ogs_assert(client);
```

на:

```c
client = something_from_remote_profile(...);

if (!client) {
    ogs_error(...);
    return NRF_VALIDATION_OR_PROCESSING_ERROR;
}
```

А верхний слой должен сформировать корректный SBI ProblemDetails.

---

# 6. P0.1 — Validation NFProfile до изменения NRF context

Нужно ввести отдельную полную validation фазу:

```text
HTTP request
    |
    v
JSON/OpenAPI decode
    |
    v
validate NFProfile
    |
    +-- ERROR -> ProblemDetails -> return
    |
    v
create/update internal NF instance
    |
    v
create clients/endpoints
    |
    v
commit registration
```

Недопустим текущий pattern:

```text
partially create NF instance
    ->
partially create NF services
    ->
partially associate clients
    ->
assert
    ->
NRF crash
```

Validation должна выполняться до destructive/committing operations.

---

# 7. P0.2 — Mandatory NFProfile fields

Проверять как минимум обязательные поля NFProfile согласно используемой версии TS 29.510/OpenAPI:

```text
nfInstanceId
nfType
nfStatus
```

Также проверить semantic consistency:

```text
NFProfile.nfInstanceId == {nfInstanceId} из URI
```

Если значения различаются:

```text
400 Bad Request
```

ProblemDetails detail:

```text
NF Instance ID in request body does not match resource URI
```

Не silently исправлять ID.

---

# 8. P0.3 — NF Instance ID validation

Проверить:

- field присутствует;
- строка не пустая;
- формат UUID соответствует требованиям применяемой версии API;
- URI ID и body ID совпадают;
- размер строки ограничен;
- parser не допускает overflow/oversized values.

Невалидный ID:

```text
400
```

Не `500`, не `assert`.

---

# 9. P0.4 — NF-level addressing validation

Согласно TS 29.510 в NFProfile должен присутствовать минимум один NF-level addressing parameter:

```text
fqdn
ipv4Addresses
ipv6Addresses
```

Минимум один из них должен быть валиден.

То есть профиль, в котором адреса существуют только здесь:

```text
nfServiceList[*].ipEndPoints
```

но отсутствуют:

```text
NFProfile.fqdn
NFProfile.ipv4Addresses
NFProfile.ipv6Addresses
```

не должен приводить к:

```text
ogs_sbi_client_associate(NULL)
assert
abort
```

Целевое поведение:

```text
400 Bad Request
ProblemDetails
```

Пример:

```json
{
  "status": 400,
  "title": "Invalid NFProfile",
  "cause": "MANDATORY_IE_MISSING",
  "detail": "At least one NF-level addressing parameter is required",
  "invalidParams": [
    {
      "param": "fqdn|ipv4Addresses|ipv6Addresses",
      "reason": "At least one addressing parameter shall be present"
    }
  ]
}
```

Использовать реальные constants/cause names Open5GS/3GPP, если они уже определены.

Не придумывать новый cause, если для этого случая уже существует стандартный.

---

# 10. P0.5 — Валидировать адреса до создания SBI client

Для:

```text
fqdn
ipv4Addresses[]
ipv6Addresses[]
NFService.ipEndPoints[]
```

проверять:

- syntax;
- допустимый address family;
- port range;
- transport enum;
- scheme compatibility;
- отсутствие пустых элементов;
- количество элементов;
- длины строк.

IPv4:

```text
0.0.0.0/invalid textual IP -> reject
```

IPv6:

```text
invalid textual IPv6 -> reject
```

Port:

```text
1..65535
```

если port присутствует и согласно schema не допускает другое.

Никакого client association до успешной validation.

---

# 11. P0.6 — Исправить `ogs_sbi_client_associate()` crash path

Найти все call sites:

```bash
grep -R "ogs_sbi_client_associate" -n .
```

Особенно проверить:

```text
lib/sbi/context.c
lib/sbi/nnrf-handler.c
src/nrf/
```

Нужно определить, почему в реальном кейсе вызывается:

```c
ogs_sbi_client_associate(..., client)
```

с:

```text
client == NULL
```

Исправить архитектурно:

1. функция, создающая/находящая client, возвращает status;
2. caller проверяет status;
3. request handler формирует ProblemDetails;
4. NF instance не остаётся в partially registered state.

Пример pattern:

```c
client = ogs_sbi_client_find_or_add(...);

if (!client) {
    ogs_error("Cannot create SBI client for NF instance [%s]",
        nf_instance_id);

    error->http_status = OGS_SBI_HTTP_STATUS_BAD_REQUEST;
    error->cause = ...;
    error->detail = ...;

    goto rollback;
}

ogs_sbi_client_associate(nf_instance, client);
```

Внутри `ogs_sbi_client_associate()` также желательно убрать fatal assert для public-facing path:

```c
if (!client) {
    ogs_error("Cannot associate NULL SBI client");
    return OGS_ERROR;
}
```

Если изменение API функции слишком большое — сделать safe wrapper.

---

# 12. P0.7 — Transactional NF registration / rollback

NFRegister должен быть атомарным на уровне NRF state.

До validation failure:

```text
NF Instance не должен стать discoverable
```

Если ошибка произошла после временного создания internal objects:

```text
rollback:
- NF service entries
- endpoint entries
- client refs
- subscriptions created as side effect
- timers
- associated context
- NF instance placeholder
```

После `400` discovery не должен возвращать partially registered NF.

Добавить проверку в tests.

---

# 13. P0.8 — `nfServiceList` map key

TS 29.510 определяет `nfServiceList` как map, где:

```text
key == NFService.serviceInstanceId
```

Проверять для каждого элемента:

```text
map_key == serviceInstanceId
```

Пример входных данных:

```json
"nfServiceList": {
  "CHF-charging": {
    "serviceInstanceId": "adbc99a1-ea87-4b98-ab36-7813f8d0f8df"
  }
}
```

не соответствует этому правилу.

Строгий режим NRF должен вернуть:

```text
400 Bad Request
```

с указанием invalid parameter.

Не переписывать ключ silent'ом.

---

# 14. P0.9 — Unique `serviceInstanceId`

В пределах одного NF Instance каждый NFService instance должен иметь уникальный:

```text
serviceInstanceId
```

Проверять duplicates до добавления в internal pool.

Проблемный пример:

```text
nchf-convergedcharging
serviceInstanceId = X

nchf-spendinglimitcontrol
serviceInstanceId = X
```

Должен вернуть:

```text
400
```

ProblemDetails detail:

```text
Duplicate NF Service Instance ID
```

Не допускать:

- overwrite;
- merging двух services;
- double-free;
- duplicate internal object;
- pool exhaustion;
- assert.

---

# 15. P0.10 — `serviceName` validation

Для каждого NFService:

```text
serviceName
```

должен:

- присутствовать, если required schema;
- быть непустым;
- корректно декодироваться;
- соответствовать supported/custom service rules применяемой версии;
- не приводить к assert при unknown/custom values.

NRF должен поддерживать стандартное поведение для custom/unknown NF types/service extensions там, где это разрешено 3GPP.

Не делать whitelist только из сервисов, известных конкретной сборке, если 29.510 требует extensibility.

---

# 16. P0.11 — NF type handling

TS 29.510 предусматривает регистрацию стандартных NF types и поддержку custom NF types согласно применяемой версии спецификации.

Нельзя:

```c
switch(nf_type) {
...
default:
    ogs_assert_if_reached();
}
```

для remote input.

Неизвестный/расширенный NF type должен:

- либо корректно храниться как custom, если OpenAPI/model позволяет;
- либо возвращать контролируемый `400` с ProblemDetails, если конкретная API version не может его представить.

Никогда не crash.

---

# 17. P0.12 — NFService versions validation

Для каждого:

```json
"versions": [
  {
    "apiVersionInUri": "...",
    "apiFullVersion": "..."
  }
]
```

проверить:

```text
versions exists where required
versions count >= 1
apiVersionInUri valid
apiFullVersion valid according to OpenAPI / 29.501 / 29.510
```

Пустая строка:

```json
"apiFullVersion": ""
```

не должна приводить к internal failure.

Если schema/semantic rules требуют non-empty value:

```text
400 Bad Request
```

с `invalidParams`.

---

# 18. P0.13 — NFService scheme / TLS consistency

Проверять:

```text
scheme = http | https
```

Если используется:

```text
https
```

проверить наличие addressing/FQDN information в соответствии с правилами TS 29.510 и общей SBI TLS model.

Не создавать unusable client, который потом упадёт при первом request.

Непоследовательный profile:

```text
400
```

---

# 19. P0.14 — `nfServiceStatus`

Проверить enum:

```text
REGISTERED
SUSPENDED
UNDISCOVERABLE
...
```

с учётом конкретной версии OpenAPI.

Unknown invalid enum:

```text
400
```

Не assert.

---

# 20. P0.15 — priority/capacity/load ranges

Валидировать числовые поля:

```text
priority
capacity
load
```

в соответствии с schema/OpenAPI limits.

Особенно:

- negative;
- integer overflow;
- слишком большие JSON numbers;
- wrong JSON type.

Неверное значение:

```text
400
```

---

# 21. P0.16 — Heartbeat timer

Проверить:

```text
heartBeatTimer
```

Не позволять:

```text
negative
overflow
invalid JSON type
```

Если NF предлагает значение, NRF должен следовать процедуре 29.510 и собственным policy/config rules.

Нельзя использовать некорректный value для создания таймера до validation.

---

# 22. P0.17 — Correct NFRegister success response

Для новой успешной регистрации:

```text
201 Created
```

Проверить:

```text
Location header
Content-Type
NFProfile response
heartbeat information
```

в соответствии с поддерживаемой API version / feature negotiation.

Не возвращать `200` только потому, что это проще, если процедура требует `201` для создания.

---

# 23. P0.18 — Complete replacement / existing NF instance

Отдельно проверить semantics:

```text
PUT /nf-instances/{id}
```

для уже существующей NF Instance.

Не путать:

- initial registration;
- complete NFProfile replacement;
- heartbeat/partial update через PATCH.

Нужно следовать процедуре NFUpdate из TS 29.510.

Не создавать второй internal NF instance для того же ID.

Не оставлять old service/client refs после complete replacement.

---

# 24. P0.19 — PATCH validation

Для:

```text
PATCH /nnrf-nfm/v1/nf-instances/{id}
```

проверять JSON Patch / supported patch operations согласно TS 29.510.

Некорректный patch:

```text
4xx + ProblemDetails
```

Никогда:

```text
assert
```

Проверить особенно:

```text
/nfStatus
/load
/heartBeatTimer
NF service related changes
```

---

# 25. P0.20 — Heartbeat behavior

Heartbeat/update существующей NF должен:

1. найти NF instance;
2. проверить lifecycle/state;
3. применить update;
4. обновить heartbeat timer;
5. вернуть корректный success status;
6. не пересоздавать SBI clients без необходимости.

Если NF instance неизвестен:

вернуть response согласно TS 29.510 для этого случая, а не создавать объект silently.

---

# 26. P0.21 — Deregistration

Для:

```text
DELETE /nnrf-nfm/v1/nf-instances/{id}
```

нужно корректно очистить:

- NF instance;
- NF services;
- endpoints;
- associated SBI clients/refcounts;
- heartbeat timer;
- discovery indexes;
- subscriptions/notifications, где применимо.

Повторный DELETE / unknown ID:

обрабатывать по TS 29.510, без assert.

---

# 27. P0.22 — Error response через `ProblemDetails`

Использовать TS 29.500 ProblemDetails.

Для invalid input включать:

```text
status
title/detail
cause — если применимо
invalidParams — где возможно
```

Пример:

```json
{
  "status": 400,
  "title": "Invalid NFProfile",
  "cause": "MANDATORY_IE_MISSING",
  "detail": "NFProfile does not contain NF-level addressing information",
  "invalidParams": [
    {
      "param": "ipv4Addresses",
      "reason": "At least one of fqdn, ipv4Addresses or ipv6Addresses shall be present"
    }
  ]
}
```

Не использовать generic:

```json
{
  "title": "server callback error",
  "status": 500
}
```

для validation failure.

---

# 28. HTTP status policy

## 400 Bad Request

Использовать при:

- invalid JSON / OpenAPI body;
- mandatory attribute missing;
- malformed attribute;
- URI/body NF Instance ID mismatch;
- invalid address;
- invalid enum;
- duplicate serviceInstanceId;
- invalid `nfServiceList` key;
- inconsistent NFService;
- semantic NFProfile validation failure.

## 404 Not Found

Использовать, когда операция обращается к resource, который должен существовать, но не найден, если это соответствует конкретной процедуре TS 29.510.

## 409 Conflict

Использовать только если конкретная процедура / application error semantics 29.510/29.500 действительно требуют conflict.

Не придумывать 409 самостоятельно.

## 5xx

Только для server-side/internal/temporary failures.

### 500

Реальная внутренняя ошибка NRF.

### 503

Temporary unavailable / dependency/resource unavailable, когда это соответствует semantics.

Не превращать malformed client request в 500/503.

---

# 29. OpenAPI validation first

До business logic максимально использовать сгенерированные OpenAPI structures.

Pipeline:

```text
HTTP bytes
  ->
JSON parse
  ->
OpenAPI decode
  ->
schema-level validation
  ->
semantic 29.510 validation
  ->
internal context creation
```

Не начинать создание NRF context после одного лишь успешного `cJSON_Parse()`.

---

# 30. Отдельный semantic validator

Создать отдельный слой, например:

```c
int nrf_validate_nf_profile(
    OpenAPI_nf_profile_t *profile,
    const char *resource_nf_instance_id,
    nrf_validation_error_t *error);
```

Или подходящий аналог в существующей архитектуре.

Validator не должен:

- менять global context;
- создавать timers;
- создавать clients;
- добавлять NF instances;
- выполнять network I/O.

Он только проверяет данные.

---

# 31. Структура validation error

Рекомендуется внутренний тип:

```c
typedef struct nrf_validation_error_s {
    int http_status;
    const char *cause;
    const char *title;
    char detail[...];

    /* optional */
    const char *param;
    const char *reason;
} nrf_validation_error_t;
```

Использовать существующий Open5GS error/ProblemDetails framework, если аналог уже есть.

Не создавать вторую несовместимую систему ошибок.

---

# 32. Validate before `ogs_sbi_nf_instance_add()`

В текущем логе видно:

```text
NFInstance added
```

до fatal failure.

Это нежелательно.

Нужно добиться:

```text
parse
validate
THEN add NFInstance
```

а не:

```text
add NFInstance
then discover profile is invalid
```

Если архитектурно требуется temporary context, он не должен публиковаться в global/discovery state до commit.

---

# 33. Safe resource limits

NRF получает не доверенные network requests.

Ввести/проверить bounds для:

```text
max NF services per NF instance
max addresses per NF
max endpoints per service
max versions per service
max info-list entries
max JSON body size
max string lengths
max nested list/map entries
```

При превышении лимита:

```text
4xx
```

или предусмотренный application error.

Не допускать exhaustion pools/assert.

---

# 34. Особое внимание Open5GS pool exhaustion

Проверить все места, где Open5GS использует fixed pools:

```text
NF instance pool
NF service pool
NF info pool
SBI client pool
subscription pool
```

Remote profile не должен исчерпать pool и вызвать:

```text
ogs_assert()
abort()
```

Все allocation functions должны корректно отдавать error.

В 2026 году уже фиксировались/обсуждались Open5GS crash cases вокруг oversized NFProfile structures и pool exhaustion — этот класс ошибок нужно закрыть системно, а не только конкретный CHF case.

---

# 35. `nfServiceList` limits

Специально добавить limit:

```text
MAX_NF_SERVICES_PER_INSTANCE
```

Значение определить из:

- существующего Open5GS pool sizing;
- конфигурации;
- reasonable operational limit.

При превышении:

```text
400/413/appropriate ProblemDetails
```

по архитектуре проекта и 29.500 semantics.

Не позволять удалённому NF исчерпать global service pool.

---

# 36. Info lists limits

Аналогично проверить:

```text
smfInfoList
udmInfoList
ausfInfoList
pcfInfoList
chfInfo/chfInfoList
upfInfo
...
```

Каждый map/list, способный порождать internal allocations, должен иметь upper bound.

---

# 37. Unknown fields / forward compatibility

Поведение должно быть совместимо с evolution 3GPP releases.

Не отвергать NFProfile только потому, что более новая версия содержит extension field, если OpenAPI/parser допускает unknown extension semantics.

Но неизвестное поле не должно менять routing/state неожиданным образом.

Сохранять forward compatibility там, где это поддерживает framework.

---

# 38. Release compatibility

Target:

```text
Release 19 baseline
```

с practical interoperability с:

```text
Release 18
```

Если поведение различается по negotiated API version/features:

- реализовать version-aware behavior;
- не смешивать rules разных releases;
- написать tests для поддерживаемых versions.

---

# 39. Feature negotiation

Проверить поддержку/обработку features:

```text
SupportedFeatures
nfProfileChangesSupportInd
nfProfilePartialUpdateChangesSupportInd
service-map related behavior
```

Если feature не поддерживается:

- корректно negotiated/ignored/rejected согласно 29.510;
- не assert.

---

# 40. `nfServices` vs `nfServiceList`

TS 29.510 содержит legacy/deprecated:

```text
nfServices
```

и preferred:

```text
nfServiceList
```

Поддержать behavior согласно negotiated Service-Map feature / release semantics.

Нельзя:

- одновременно импортировать оба и создавать duplicates;
- silently merge inconsistent representations.

Если оба присутствуют и конфликтуют — обработать согласно specification / feature semantics.

---

# 41. NRF must not build unusable clients

Не каждая зарегистрированная NFService автоматически означает, что NRF сам должен создавать transport client ко всем endpoints прямо во время регистрации.

Проверить архитектуру Open5GS:

- зачем NRF создаёт client;
- нужен ли NF-level client;
- нужен ли service-level client;
- можно ли lazy-create client при реальной необходимости.

Если client нужен только для notification/callback:

предпочесть lazy/safe client construction после полной validation.

Это уменьшает attack surface и вероятность registration-time crash.

---

# 42. Duplicate addresses/endpoints

Profile может содержать duplicate:

```text
ipv4Addresses
ipEndPoints
```

NRF должен:

- либо canonicalize/deduplicate, если specification допускает;
- либо принять duplicate без создания duplicate internal client/ref;
- либо reject, если это violates semantic rule.

Нельзя создавать несколько одинаковых SBI clients без необходимости.

---

# 43. Endpoint precedence

Проверить правила выбора:

```text
NFService.fqdn
NFService.ipEndPoints
NFProfile.fqdn
NFProfile.ipv4Addresses
NFProfile.ipv6Addresses
```

Использовать precedence из TS 29.510.

Особенно важно для HTTPS/TLS.

Не придумывать собственный fallback order.

---

# 44. Logging

При failed NFRegister лог должен быть диагностическим:

```text
[NRF] NFRegister rejected
  nfInstanceId=...
  nfType=CHF
  reason=missing-nf-level-address
  http_status=400
```

Для NFService:

```text
[NRF] Invalid NFService
  nfInstanceId=...
  serviceInstanceId=...
  serviceName=...
  reason=duplicate-service-instance-id
```

Не логировать:

- credentials;
- auth tokens;
- sensitive subscriber data.

---

# 45. Убрать misleading log

Текущий:

```text
[sbi] DEBUG: [NULL] NFInstance added with Ref [(null)]
```

нужно расследовать.

Для normal registration не должно быть meaningless:

```text
[NULL]
[(null)]
```

Нужно:

- либо исправить log context;
- либо не логировать object как successfully added до validation/commit.

---

# 46. Regression test: исходный crash

Обязательный test.

Input:

```text
PUT /nnrf-nfm/v1/nf-instances/<UUID>
```

NFProfile:

- `nfType=CHF`;
- нет NFProfile `fqdn`;
- нет `ipv4Addresses`;
- нет `ipv6Addresses`;
- service-level endpoint `172.16.74.1:29643` присутствует.

Expected:

```text
400 Bad Request
ProblemDetails
```

Process:

```text
NRF remains alive
```

Обязательно проверить:

```text
kill -0 <nrf-pid>
```

или equivalent test harness assertion.

---

# 47. Regression test: valid CHF

Профиль:

```json
{
  "nfInstanceId": "<UUID>",
  "nfType": "CHF",
  "nfStatus": "REGISTERED",
  "ipv4Addresses": ["172.16.74.1"],
  "nfServiceList": {
    "<service-uuid-1>": {
      "serviceInstanceId": "<service-uuid-1>",
      "serviceName": "nchf-convergedcharging",
      "versions": [{
        "apiVersionInUri": "v1",
        "apiFullVersion": "<valid-version>"
      }],
      "scheme": "http",
      "nfServiceStatus": "REGISTERED",
      "ipEndPoints": [{
        "ipv4Address": "172.16.74.1",
        "transport": "TCP",
        "port": 29643
      }]
    }
  }
}
```

Expected:

```text
201 Created
Location present
NF registered
NRF alive
```

---

# 48. Regression test: map key mismatch

Input:

```json
"nfServiceList": {
  "wrong-key": {
    "serviceInstanceId": "<UUID>"
  }
}
```

Expected:

```text
400
ProblemDetails
NRF alive
```

---

# 49. Regression test: duplicate `serviceInstanceId`

Два services:

```text
service A ID = X
service B ID = X
```

Expected:

```text
400
NRF alive
no partial registration
```

---

# 50. Regression test: malformed address

```json
"ipv4Addresses": ["999.999.1.1"]
```

Expected:

```text
400
NRF alive
```

---

# 51. Regression test: invalid service endpoint

```json
"port": 70000
```

Expected:

```text
400
NRF alive
```

---

# 52. Regression test: empty API version

```json
"apiFullVersion": ""
```

Expected:

согласно OpenAPI/3GPP semantic validation:

```text
400
```

если empty value invalid для используемой schema/version.

Главное:

```text
NRF alive
```

---

# 53. Regression test: oversized `nfServiceList`

Создать payload с количеством services выше configured/safe limit.

Expected:

```text
controlled error response
NRF alive
memory bounded
no pool assert
```

---

# 54. Regression test: huge info list

Создать oversized:

```text
smfInfoList
```

или другой map/list, который аллоцирует internal objects.

Expected:

```text
controlled reject
NRF alive
```

---

# 55. Regression test: URI/body ID mismatch

URI:

```text
/nf-instances/A
```

Body:

```text
nfInstanceId=B
```

Expected:

```text
400
```

---

# 56. Regression test: valid PATCH heartbeat

После valid registration:

```http
PATCH /nnrf-nfm/v1/nf-instances/<id>
```

Body:

```json
[
  {
    "op": "replace",
    "path": "/nfStatus",
    "value": "REGISTERED"
  },
  {
    "op": "replace",
    "path": "/load",
    "value": 0
  }
]
```

Expected:

```text
204 No Content
```

и NRF остаётся alive.

---

# 57. Regression test: malformed PATCH

Unsupported path/type/op:

Expected:

```text
4xx ProblemDetails
NRF alive
```

---

# 58. Regression test: complete replacement

1. register NF;
2. PUT profile с тем же NF Instance ID и изменёнными services;
3. проверить:
   - old services удалены;
   - new services появились;
   - no stale clients;
   - no ref leak;
   - no duplicate NF instance.

---

# 59. Regression test: delete

После registration:

```http
DELETE /nnrf-nfm/v1/nf-instances/<id>
```

Expected:

- NF больше не discoverable;
- resources released;
- NRF alive.

---

# 60. Fuzzing target

Добавить fuzz target для:

```text
NFRegister NFProfile
```

Минимум:

```text
JSON/OpenAPI parser
semantic validator
nfServiceList
addressing
info lists
versions
```

Invariant:

> Ни один fuzz input не должен приводить к assert/abort/segfault/use-after-free/out-of-bounds.

---

# 61. Sanitizers

Прогнать tests с:

```text
ASan
UBSan
```

По возможности:

```text
LSan
```

Проверить:

- invalid free;
- double free;
- leak;
- UAF;
- integer overflow;
- null dereference.

---

# 62. Concurrency / repeated registration

Проверить:

```text
parallel PUT same nfInstanceId
PUT + PATCH race
PUT + DELETE race
heartbeat during replacement
```

NRF state не должен corruption/crash.

Если Open5GS event loop serializes это naturally — всё равно добавить regression scenario где возможно.

---

# 63. Internal lifecycle

Для NF instance явно зафиксировать lifecycle:

```text
NEW
VALIDATING
REGISTERED
SUSPENDED/etc.
REMOVING
```

NF не должен попадать в discoverable registry до successful commit.

---

# 64. Проверить discovery после регистрации

После valid NFRegister:

```text
Nnrf_NFDiscovery
```

должен вернуть зарегистрированный CHF для:

```text
target-nf-type=CHF
service-names=nchf-convergedcharging
```

с корректным endpoint.

После invalid registration:

тот же discovery не должен возвращать NF.

---

# 65. Проверить subscription notifications

Если registration/update должен вызвать NRF notification subscriptions:

- notification отправляется только после successful commit;
- invalid profile не вызывает notification;
- complete replacement содержит корректный profile;
- notification failure не должен откатывать валидную registration без основания в spec.

---

# 66. Проверить persistence/state assumptions

Если NRF state только memory:

убедиться, что rollback полностью очищает memory state.

Если есть persistence/cache:

invalid registration не должен оставлять запись.

---

# 67. Файлы для анализа

Начать с:

```text
src/nrf/nrf-sm.c
src/nrf/nf-sm.c
src/nrf/nnrf-handler.c
src/nrf/context.c
src/nrf/context.h

lib/sbi/context.c
lib/sbi/context.h
lib/sbi/nnrf-handler.c
lib/sbi/nnrf-build.c
lib/sbi/message.c
lib/sbi/types.h

lib/sbi/openapi/
```

Найти:

```bash
grep -R "ogs_sbi_client_associate" -n .
grep -R "NFInstance added" -n .
grep -R "ogs_assert(client" -n lib src
grep -R "nfServiceList" -n lib src
grep -R "service_instance_id" -n lib src
grep -R "ipv4Addresses" -n lib src
grep -R "nf_profile" -n src/nrf lib/sbi
```

---

# 68. Audit всех assert в NRF request path

Выполнить:

```bash
grep -R "ogs_assert" -n src/nrf lib/sbi
```

Для каждого assert определить:

```text
может ли его condition зависеть от remote SBI request?
```

Если да — заменить на validation/error handling.

Особенно:

- decoded pointers;
- lists/maps;
- addresses;
- service/client creation;
- pool allocations;
- enum conversions.

---

# 69. Не делать workaround только под CHF

Запрещён такой код:

```c
if (nf_type == CHF && vendor == "nexign") {
    ...
}
```

NRF должен быть generic 3GPP repository.

Тот же validator должен корректно работать для:

```text
AMF
SMF
PCF
UDM
UDR
AUSF
NSSF
CHF
NEF
NWDAF
SCP
SEPP
BSF
UPF
custom NF
...
```

с учётом profile-specific attributes.

---

# 70. Strict vs interoperability mode

Основной режим должен соответствовать 3GPP.

Если проекту действительно необходим compatibility mode для внешних NF с minor deviations — сделать его отдельно и явно.

Например:

```yaml
nrf:
  validation:
    mode: strict
```

или:

```text
strict
compatible
```

Но НЕ вводить compatibility mode без необходимости.

В `strict`:

- нарушенный `nfServiceList` key -> 400;
- missing mandatory NF address -> 400;
- duplicate serviceInstanceId -> 400.

Возможный `compatible` режим должен:

- никогда не угадывать security-sensitive semantics;
- логировать normalization;
- не скрывать существенные protocol violations;
- быть opt-in.

Сначала реализовать strict standard behavior.

---

# 71. Не normalise profile silently в strict mode

Не добавлять автоматически:

```text
NFProfile.ipv4Addresses
```

из:

```text
NFService.ipEndPoints
```

в strict mode.

Это меняет присланный NFProfile и маскирует violation.

Правильное strict поведение:

```text
400 ProblemDetails
```

Если позже нужен compatibility mode, inference может быть отдельной явно включаемой функцией.

---

# 72. Не исправлять `nfServiceList` map key silent'ом

Если:

```text
key != serviceInstanceId
```

strict mode:

```text
400
```

Не:

```text
rewrite key and accept
```

---

# 73. Не исправлять duplicate serviceInstanceId

Duplicate IDs — semantic ambiguity.

Strict mode:

```text
400
```

Не генерировать новый UUID вместо NF.

---

# 74. Нормативные ссылки для Cursor

Использовать актуальные официальные ETSI copies:

```text
3GPP TS 29.510 Release 19
ETSI TS 129 510 V19.6.0
https://www.etsi.org/deliver/etsi_ts/129500_129599/129510/19.06.00_60/ts_129510v190600p.pdf
```

```text
3GPP TS 29.500 Release 19
ETSI TS 129 500 V19.6.0
https://www.etsi.org/deliver/etsi_ts/129500_129599/129500/19.06.00_60/ts_129500v190600p.pdf
```

Для совместимости также сверить:

```text
ETSI TS 129 510 V18.11.0
```

Не ориентироваться на random blog/StackOverflow как на нормативный источник.

---

# 75. Особо проверить следующие положения TS 29.510

Cursor должен самостоятельно открыть актуальную спецификацию и проверить номера clauses/tables перед финальным commit.

Нужно подтвердить:

1. NFRegister procedure.
2. NFUpdate procedure.
3. NFDeregister procedure.
4. NFProfile data type.
5. NFService data type.
6. mandatory/optional fields.
7. `nfServiceList` map semantics.
8. addressing requirement.
9. NFService version semantics.
10. NF status.
11. heartbeat behavior.
12. error responses.
13. feature negotiation.
14. discovery visibility after registration.

Не вставлять номер clause наугад.

---

# 76. Acceptance criteria — crash case

Исходный profile больше не вызывает:

```text
ogs_sbi_client_associate: Assertion `client' failed
```

NRF не завершается.

В strict mode NRF отвечает:

```text
400 Bad Request
ProblemDetails
```

если профиль нарушает обязательные правила TS 29.510.

---

# 77. Acceptance criteria — valid CHF

После исправления profile:

```text
NF-level ipv4Addresses present
nfServiceList keys == serviceInstanceId
serviceInstanceId unique
valid versions
valid endpoints
```

регистрация проходит:

```text
201 Created
```

CHF появляется в NRF discovery.

Heartbeat проходит.

---

# 78. Acceptance criteria — process survival

После каждого negative test:

```text
NRF PID unchanged/alive
NRF accepts next valid request
```

Это обязательный критерий.

Просто systemd restart после crash не считается успехом.

---

# 79. Acceptance criteria — no partial state

После rejected NFRegister:

```text
GET/discovery
```

не должен находить эту NF.

Не должно оставаться:

- NF instance;
- NF services;
- SBI clients;
- timers;
- leaked references.

---

# 80. Acceptance criteria — memory

Прогнать несколько тысяч invalid registration requests.

RSS не должен расти линейно.

Не должно быть:

- client leak;
- service leak;
- NF instance leak;
- timer leak;
- transaction leak.

---

# 81. Acceptance criteria — existing Open5GS NF

После патча проверить штатные:

```text
AMF
AUSF
UDM
UDR
PCF
SMF
SCP
```

Все должны продолжить:

- NFRegister;
- heartbeat;
- discovery;
- deregistration.

---

# 82. Acceptance criteria — error quality

Вместо:

```text
FATAL
assert
server callback error
```

ожидаются понятные сообщения:

```text
NFRegister rejected: missing NF-level addressing
```

и корректный ProblemDetails.

---

# 83. Definition of Done

Работа считается завершённой, когда:

1. Точный crash path `client == NULL` найден и документирован.
2. Ни один remote NFProfile не способен вызвать этот assert.
3. Добавлена pre-validation NFProfile.
4. Mandatory NFProfile fields валидируются.
5. NF-level addressing валидируется.
6. `nfServiceList` key валидируется.
7. `serviceInstanceId` uniqueness валидируется.
8. NFService versions валидируются.
9. endpoints валидируются.
10. registration commit атомарен.
11. errors возвращаются через ProblemDetails.
12. malformed request -> 4xx, не generic 500.
13. internal failure -> корректный 5xx, без crash.
14. PATCH lifecycle работает.
15. DELETE lifecycle работает.
16. discovery не видит rejected NF.
17. resource limits добавлены.
18. pool exhaustion не приводит к assert.
19. regression tests добавлены.
20. ASan/UBSan tests проходят.
21. existing Open5GS NF не сломаны.
22. валидный внешний CHF успешно регистрируется.
23. strict mode не содержит vendor-specific workaround.

---

# 84. Рекомендуемые commits

## Commit 1

```text
nrf: add semantic NFProfile validation before registration
```

## Commit 2

```text
sbi: make client association failures non-fatal
```

## Commit 3

```text
nrf: validate NF service map, identifiers and endpoints
```

## Commit 4

```text
nrf: add transactional registration rollback
```

## Commit 5

```text
nrf: return 3GPP ProblemDetails for NF management validation errors
```

## Commit 6

```text
nrf: harden NFProfile collection limits and pool allocation failures
```

## Commit 7

```text
tests: add malformed NFProfile and NRF crash regression coverage
```

---

# 85. Инструкция Cursor перед изменениями

Перед написанием патча:

1. Проследить полный call chain реального crash:

```text
PUT /nnrf-nfm/v1/nf-instances/{id}
 ->
nghttp2 server
 ->
SBI message decode
 ->
NRF state machine
 ->
NFRegister handler
 ->
NFProfile parser/import
 ->
NF instance/service creation
 ->
SBI client find/add
 ->
ogs_sbi_client_associate()
 ->
assert(client)
```

2. Указать конкретно, почему `client` становится `NULL`.

3. Найти, какой field profile используется для создания NF-level client.

4. Не делать предположение без проверки кода.

5. Перед patch показать root cause комментариями/commit description.

6. После patch добавить тест, который воспроизводил старый crash.

---

# 86. Итоговая целевая архитектура

```text
Remote NF
   |
   | NFRegister
   v
HTTP/SBI decoder
   |
   v
OpenAPI schema validation
   |
   v
3GPP semantic NFProfile validator
   |
   +---------------- INVALID ----------------+
   |                                         |
   |                                         v
   |                               400 + ProblemDetails
   |                               NRF remains alive
   |
   v VALID
Temporary registration context
   |
   v
Create NF services/endpoints/clients safely
   |
   +------------- internal error ------------+
   |                                         |
   |                                         v
   |                                   rollback
   |                                         |
   |                                         v
   |                                5xx ProblemDetails
   |                                NRF remains alive
   |
   v
Atomic commit
   |
   v
REGISTERED / discoverable
   |
   v
201 Created
```

---

# 87. Главный принцип

> Open5GS NRF должен вести себя как сетевой 3GPP NF, а не как тестовая программа, доверяющая peer'у.

Удалённая NF может:

- иметь другую реализацию;
- использовать другой Release;
- прислать некорректный profile;
- иметь bug;
- быть malicious.

NRF обязан:

```text
validate -> reject/accept -> stay alive
```

и никогда:

```text
parse -> assert -> abort
```.
