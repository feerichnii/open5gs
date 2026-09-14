#include "ogs-sbi.h"
#include "jwt.h"
#include "oauth.h"

static bool uri_is_oauth_exempt(const char *uri)
{
    if (!uri)
        return true;
    if (strstr(uri, "/nnrf-nfm/") || strstr(uri, "/oauth2/"))
        return true;
    return false;
}

bool ogs_sbi_oauth_server_authorize(
        ogs_sbi_stream_t *stream, ogs_sbi_request_t *request)
{
    ogs_hash_index_t *hi = NULL;
    const char *auth = NULL;
    char *payload = NULL;
    cJSON *root = NULL, *exp = NULL, *aud = NULL;
    ogs_time_t now;
    const char *secret;

    if (!ogs_sbi_self()->oauth2.enabled)
        return true;

    ogs_assert(stream);
    ogs_assert(request);

    if (uri_is_oauth_exempt(request->h.uri))
        return true;

    for (hi = ogs_hash_first(request->http.headers);
            hi; hi = ogs_hash_next(hi)) {
        const char *key = ogs_hash_this_key(hi);
        if (key && !strcasecmp(key, "Authorization"))
            auth = ogs_hash_this_val(hi);
    }

    if (!auth || strncasecmp(auth, "Bearer ", 7) != 0) {
        ogs_sbi_server_send_error(stream, OGS_SBI_HTTP_STATUS_UNAUTHORIZED,
                NULL, "Missing or invalid Bearer token", NULL,
                OGS_SBI_CAUSE_AUTHENTICATION_REJECTED);
        return false;
    }

    secret = ogs_sbi_self()->oauth2.signing_key ?
        ogs_sbi_self()->oauth2.signing_key : "open5gs-lab-key";

    if (!ogs_sbi_jwt_verify_hs256(auth + 7, secret, &payload)) {
        ogs_sbi_server_send_error(stream, OGS_SBI_HTTP_STATUS_UNAUTHORIZED,
                NULL, "Invalid access token", NULL,
                OGS_SBI_CAUSE_AUTHENTICATION_REJECTED);
        return false;
    }

    root = cJSON_Parse(payload);
    ogs_free(payload);
    if (!root) {
        ogs_sbi_server_send_error(stream, OGS_SBI_HTTP_STATUS_UNAUTHORIZED,
                NULL, "Invalid token payload", NULL,
                OGS_SBI_CAUSE_AUTHENTICATION_REJECTED);
        return false;
    }

    now = ogs_time_now();
    exp = cJSON_GetObjectItemCaseSensitive(root, "exp");
    if (!exp || !cJSON_IsNumber(exp) ||
            (ogs_time_t)exp->valuedouble < ogs_time_sec(now)) {
        cJSON_Delete(root);
        ogs_sbi_server_send_error(stream, OGS_SBI_HTTP_STATUS_UNAUTHORIZED,
                NULL, "Token expired", NULL,
                OGS_SBI_CAUSE_AUTHENTICATION_REJECTED);
        return false;
    }

    aud = cJSON_GetObjectItemCaseSensitive(root, "aud");
    if (aud && cJSON_IsString(aud) && aud->valuestring &&
            ogs_sbi_self()->nf_instance) {
        const char *my_type = OpenAPI_nf_type_ToString(
                ogs_sbi_self()->nf_instance->nf_type);
        if (my_type && strcmp(aud->valuestring, my_type) != 0 &&
                strcmp(aud->valuestring,
                    ogs_sbi_self()->nf_instance->id) != 0) {
            cJSON_Delete(root);
            ogs_sbi_server_send_error(stream, OGS_SBI_HTTP_STATUS_FORBIDDEN,
                    NULL, "Token audience mismatch", NULL,
                    OGS_SBI_CAUSE_AUTHENTICATION_REJECTED);
            return false;
        }
    }

    /* Optional scope check: token scope must include a service name from URI */
    {
        cJSON *scope_item = cJSON_GetObjectItemCaseSensitive(root, "scope");
        if (scope_item && cJSON_IsString(scope_item) &&
                scope_item->valuestring && request->h.uri) {
            const char *scope_str = scope_item->valuestring;
            const char *uri = request->h.uri;
            const char *svc = NULL;

            if (strstr(uri, "/nudm-"))
                svc = strstr(uri, "/nudm-");
            else if (strstr(uri, "/namf-"))
                svc = strstr(uri, "/namf-");
            else if (strstr(uri, "/npcf-"))
                svc = strstr(uri, "/npcf-");
            else if (strstr(uri, "/nsmf-"))
                svc = strstr(uri, "/nsmf-");
            else if (strstr(uri, "/nnrf-"))
                svc = strstr(uri, "/nnrf-");
            else if (strstr(uri, "/nchf-"))
                svc = strstr(uri, "/nchf-");
            else if (strstr(uri, "/nausf-"))
                svc = strstr(uri, "/nausf-");

            if (svc) {
                char needed[64];
                const char *slash2;
                size_t len;

                svc++; /* skip leading '/' */
                slash2 = strchr(svc, '/');
                len = slash2 ? (size_t)(slash2 - svc) : strlen(svc);
                if (len >= sizeof(needed))
                    len = sizeof(needed) - 1;
                memcpy(needed, svc, len);
                needed[len] = '\0';

                if (needed[0] && !strstr(scope_str, needed) &&
                        strcmp(scope_str, "default") != 0) {
                    cJSON_Delete(root);
                    ogs_sbi_server_send_error(stream,
                            OGS_SBI_HTTP_STATUS_FORBIDDEN,
                            NULL, "Token scope mismatch", needed,
                            OGS_SBI_CAUSE_AUTHENTICATION_REJECTED);
                    return false;
                }
            }
        }
    }

    cJSON_Delete(root);
    return true;
}

char *ogs_sbi_oauth_issue_access_token(
        const char *nf_instance_id, OpenAPI_nf_type_e nf_type,
        OpenAPI_nf_type_e target_nf_type, const char *scope)
{
    char *payload = NULL;
    char *jwt = NULL;
    ogs_time_t now = ogs_time_now();
    ogs_time_t exp = now + ogs_time_from_sec(ogs_sbi_self()->oauth2.token_ttl);
    const char *secret = ogs_sbi_self()->oauth2.signing_key ?
        ogs_sbi_self()->oauth2.signing_key : "open5gs-lab-key";
    const char *aud;

    ogs_assert(nf_instance_id);
    (void)nf_type;

    aud = target_nf_type ?
        OpenAPI_nf_type_ToString(target_nf_type) : "NRF";

    payload = ogs_msprintf(
            "{\"iss\":\"%s\",\"sub\":\"%s\",\"aud\":\"%s\","
            "\"scope\":\"%s\",\"exp\":%lld,\"iat\":%lld}",
            ogs_sbi_self()->oauth2.issuer ?
                ogs_sbi_self()->oauth2.issuer : "nrf.5gc.lab",
            nf_instance_id,
            aud,
            scope ? scope : "nnrf-nfm",
            (long long)ogs_time_sec(exp),
            (long long)ogs_time_sec(now));
    ogs_assert(payload);

    jwt = ogs_sbi_jwt_encode_hs256(payload, secret);
    ogs_free(payload);
    return jwt;
}

/* Parse application/x-www-form-urlencoded body into strdup'd values */
bool ogs_sbi_oauth_parse_token_form(
        const char *body,
        char **grant_type,
        char **nf_instance_id,
        char **nf_type,
        char **target_nf_type,
        char **scope,
        char **target_nf_instance_id)
{
    char *copy = NULL, *p = NULL, *save = NULL;

    if (!body)
        return false;

    if (grant_type) *grant_type = NULL;
    if (nf_instance_id) *nf_instance_id = NULL;
    if (nf_type) *nf_type = NULL;
    if (target_nf_type) *target_nf_type = NULL;
    if (scope) *scope = NULL;
    if (target_nf_instance_id) *target_nf_instance_id = NULL;

    copy = ogs_strdup(body);
    ogs_assert(copy);

    for (p = strtok_r(copy, "&", &save); p; p = strtok_r(NULL, "&", &save)) {
        char *eq = strchr(p, '=');
        char *key, *val, *decoded;

        if (!eq)
            continue;
        *eq = '\0';
        key = p;
        val = eq + 1;
        decoded = ogs_sbi_url_decode(val);
        if (!decoded)
            decoded = ogs_strdup(val);

        if (!strcmp(key, "grant_type") && grant_type)
            *grant_type = decoded;
        else if (!strcmp(key, "nfInstanceId") && nf_instance_id)
            *nf_instance_id = decoded;
        else if (!strcmp(key, "nfType") && nf_type)
            *nf_type = decoded;
        else if (!strcmp(key, "targetNfType") && target_nf_type)
            *target_nf_type = decoded;
        else if (!strcmp(key, "scope") && scope)
            *scope = decoded;
        else if (!strcmp(key, "targetNfInstanceId") && target_nf_instance_id)
            *target_nf_instance_id = decoded;
        else
            ogs_free(decoded);
    }

    ogs_free(copy);
    return true;
}
