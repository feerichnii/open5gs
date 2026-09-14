#include "ogs-sbi.h"
#include "jwt.h"
#include "oauth.h"

static bool uri_is_oauth_exempt(const char *uri)
{
    if (!uri)
        return true;
    if (strstr(uri, "/nnrf-nfm/") || strstr(uri, "/oauth2/"))
        return true;
    if (strstr(uri, "/nnrf-disc/"))
        return false;
    return false;
}

bool ogs_sbi_oauth_server_authorize(
        ogs_sbi_stream_t *stream, ogs_sbi_request_t *request)
{
    ogs_hash_index_t *hi = NULL;
    const char *auth = NULL;

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

    if (!ogs_sbi_jwt_verify_hs256(auth + 7,
                ogs_sbi_self()->oauth2.signing_key ?
                    ogs_sbi_self()->oauth2.signing_key : "open5gs-lab-key",
                NULL)) {
        ogs_sbi_server_send_error(stream, OGS_SBI_HTTP_STATUS_UNAUTHORIZED,
                NULL, "Invalid access token", NULL,
                OGS_SBI_CAUSE_AUTHENTICATION_REJECTED);
        return false;
    }

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

    ogs_assert(nf_instance_id);

    payload = ogs_msprintf(
            "{\"iss\":\"%s\",\"sub\":\"%s\",\"aud\":\"%s\","
            "\"scope\":\"%s\",\"exp\":%lld,\"iat\":%lld}",
            ogs_sbi_self()->oauth2.issuer ?
                ogs_sbi_self()->oauth2.issuer : "nrf.5gc.lab",
            nf_instance_id,
            OpenAPI_nf_type_ToString(target_nf_type),
            scope ? scope : "nnrf-nfm",
            (long long)ogs_time_sec(exp),
            (long long)ogs_time_sec(now));
    ogs_assert(payload);

    jwt = ogs_sbi_jwt_encode_hs256(payload, secret);
    ogs_free(payload);
    return jwt;
}
