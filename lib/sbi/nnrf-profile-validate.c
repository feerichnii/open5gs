/*
 * Copyright (C) 2026 Open5GS Model D lab contributors
 *
 * Strict semantic NFProfile validation before NRF registration commit.
 */

#include "ogs-sbi.h"
#include "nnrf-profile-validate.h"

#include <stdarg.h>

#define OGS_NNRF_MAX_NF_SERVICES        64
#define OGS_NNRF_MAX_IP_ENDPOINTS       16
#define OGS_NNRF_MAX_NF_ADDRESSES       16

static int validation_fail(
        ogs_nnrf_profile_validation_t *err,
        int status,
        const char *cause,
        const char *title,
        const char *invalid_param,
        const char *fmt, ...)
{
    va_list ap;

    if (err) {
        memset(err, 0, sizeof(*err));
        err->status = status;
        err->cause = cause;
        err->title = title;
        err->invalid_param = invalid_param;
        va_start(ap, fmt);
        ogs_vsnprintf(err->detail, sizeof(err->detail), fmt, ap);
        va_end(ap);
    }

    ogs_error("NFProfile validation failed: %s",
            err && err->detail[0] ? err->detail : title);
    return OGS_ERROR;
}

static bool ipv4_ok(const char *s)
{
    ogs_sockaddr_t tmp;

    if (!s || !s[0] || !strcmp(s, "0.0.0.0"))
        return false;
    return ogs_inet_pton(AF_INET, s, &tmp) == OGS_OK;
}

static bool ipv6_ok(const char *s)
{
    ogs_sockaddr_t tmp;

    if (!s || !s[0])
        return false;
    return ogs_inet_pton(AF_INET6, s, &tmp) == OGS_OK;
}

static int validate_ip_end_points(
        OpenAPI_list_t *endpoints,
        const char *svc_id,
        ogs_nnrf_profile_validation_t *err)
{
    OpenAPI_lnode_t *node;
    int n = 0;

    if (!endpoints)
        return OGS_OK;

    OpenAPI_list_for_each(endpoints, node) {
        OpenAPI_ip_end_point_t *ep = node->data;

        n++;
        if (n > OGS_NNRF_MAX_IP_ENDPOINTS)
            return validation_fail(err, OGS_SBI_HTTP_STATUS_BAD_REQUEST,
                    OGS_SBI_CAUSE_OPTIONAL_IE_INCORRECT,
                    "Invalid NFProfile", "ipEndPoints",
                    "Too many ipEndPoints for service [%s]",
                    svc_id ? svc_id : "?");

        if (!ep)
            return validation_fail(err, OGS_SBI_HTTP_STATUS_BAD_REQUEST,
                    OGS_SBI_CAUSE_MANDATORY_IE_INCORRECT,
                    "Invalid NFProfile", "ipEndPoints",
                    "Null ipEndPoint in service [%s]",
                    svc_id ? svc_id : "?");

        if (!ep->ipv4_address && !ep->ipv6_address)
            return validation_fail(err, OGS_SBI_HTTP_STATUS_BAD_REQUEST,
                    OGS_SBI_CAUSE_MANDATORY_IE_MISSING,
                    "Invalid NFProfile", "ipEndPoints",
                    "ipEndPoint without address in service [%s]",
                    svc_id ? svc_id : "?");

        if (ep->ipv4_address && !ipv4_ok(ep->ipv4_address))
            return validation_fail(err, OGS_SBI_HTTP_STATUS_BAD_REQUEST,
                    OGS_SBI_CAUSE_MANDATORY_IE_INCORRECT,
                    "Invalid NFProfile", "ipEndPoints.ipv4Address",
                    "Invalid IPv4 [%s] in service [%s]",
                    ep->ipv4_address, svc_id ? svc_id : "?");

        if (ep->ipv6_address && !ipv6_ok(ep->ipv6_address))
            return validation_fail(err, OGS_SBI_HTTP_STATUS_BAD_REQUEST,
                    OGS_SBI_CAUSE_MANDATORY_IE_INCORRECT,
                    "Invalid NFProfile", "ipEndPoints.ipv6Address",
                    "Invalid IPv6 [%s] in service [%s]",
                    ep->ipv6_address, svc_id ? svc_id : "?");

        if (ep->is_port && (ep->port < 1 || ep->port > 65535))
            return validation_fail(err, OGS_SBI_HTTP_STATUS_BAD_REQUEST,
                    OGS_SBI_CAUSE_OPTIONAL_IE_INCORRECT,
                    "Invalid NFProfile", "ipEndPoints.port",
                    "Invalid port [%d] in service [%s]",
                    ep->port, svc_id ? svc_id : "?");
    }

    return OGS_OK;
}

static int validate_one_service(
        OpenAPI_nf_service_t *svc,
        const char *map_key,
        ogs_nnrf_profile_validation_t *err)
{
    OpenAPI_lnode_t *vnode;
    int vn = 0;

    if (!svc)
        return validation_fail(err, OGS_SBI_HTTP_STATUS_BAD_REQUEST,
                OGS_SBI_CAUSE_MANDATORY_IE_MISSING,
                "Invalid NFProfile", "nfServiceList",
                "Null NFService entry");

    if (!svc->service_instance_id || !svc->service_instance_id[0])
        return validation_fail(err, OGS_SBI_HTTP_STATUS_BAD_REQUEST,
                OGS_SBI_CAUSE_MANDATORY_IE_MISSING,
                "Invalid NFProfile", "serviceInstanceId",
                "Missing serviceInstanceId");

    if (strlen(svc->service_instance_id) > 64)
        return validation_fail(err, OGS_SBI_HTTP_STATUS_BAD_REQUEST,
                OGS_SBI_CAUSE_MANDATORY_IE_INCORRECT,
                "Invalid NFProfile", "serviceInstanceId",
                "serviceInstanceId too long");

    if (map_key && strcmp(map_key, svc->service_instance_id) != 0)
        return validation_fail(err, OGS_SBI_HTTP_STATUS_BAD_REQUEST,
                OGS_SBI_CAUSE_MANDATORY_IE_INCORRECT,
                "Invalid NFProfile", "nfServiceList",
                "nfServiceList key [%s] does not match serviceInstanceId [%s]",
                map_key, svc->service_instance_id);

    if (!svc->service_name)
        return validation_fail(err, OGS_SBI_HTTP_STATUS_BAD_REQUEST,
                OGS_SBI_CAUSE_MANDATORY_IE_MISSING,
                "Invalid NFProfile", "serviceName",
                "Missing or unknown serviceName in [%s]",
                svc->service_instance_id);

    if (!svc->scheme)
        return validation_fail(err, OGS_SBI_HTTP_STATUS_BAD_REQUEST,
                OGS_SBI_CAUSE_MANDATORY_IE_MISSING,
                "Invalid NFProfile", "scheme",
                "Missing scheme in service [%s]",
                svc->service_instance_id);

    if (!svc->nf_service_status)
        return validation_fail(err, OGS_SBI_HTTP_STATUS_BAD_REQUEST,
                OGS_SBI_CAUSE_MANDATORY_IE_MISSING,
                "Invalid NFProfile", "nfServiceStatus",
                "Missing nfServiceStatus in service [%s]",
                svc->service_instance_id);

    if (!svc->versions || svc->versions->count < 1)
        return validation_fail(err, OGS_SBI_HTTP_STATUS_BAD_REQUEST,
                OGS_SBI_CAUSE_MANDATORY_IE_MISSING,
                "Invalid NFProfile", "versions",
                "Missing versions in service [%s]",
                svc->service_instance_id);

    OpenAPI_list_for_each(svc->versions, vnode) {
        OpenAPI_nf_service_version_t *ver = vnode->data;
        vn++;
        if (!ver || !ver->api_version_in_uri || !ver->api_version_in_uri[0])
            return validation_fail(err, OGS_SBI_HTTP_STATUS_BAD_REQUEST,
                    OGS_SBI_CAUSE_MANDATORY_IE_MISSING,
                    "Invalid NFProfile", "versions.apiVersionInUri",
                    "Empty apiVersionInUri in service [%s]",
                    svc->service_instance_id);
        if (!ver->api_full_version || !ver->api_full_version[0])
            return validation_fail(err, OGS_SBI_HTTP_STATUS_BAD_REQUEST,
                    OGS_SBI_CAUSE_MANDATORY_IE_MISSING,
                    "Invalid NFProfile", "versions.apiFullVersion",
                    "Empty apiFullVersion in service [%s]",
                    svc->service_instance_id);
    }

    if (svc->is_priority && (svc->priority < 0 || svc->priority > 65535))
        return validation_fail(err, OGS_SBI_HTTP_STATUS_BAD_REQUEST,
                OGS_SBI_CAUSE_OPTIONAL_IE_INCORRECT,
                "Invalid NFProfile", "priority",
                "priority out of range in service [%s]",
                svc->service_instance_id);

    if (svc->is_capacity && (svc->capacity < 0 || svc->capacity > 65535))
        return validation_fail(err, OGS_SBI_HTTP_STATUS_BAD_REQUEST,
                OGS_SBI_CAUSE_OPTIONAL_IE_INCORRECT,
                "Invalid NFProfile", "capacity",
                "capacity out of range in service [%s]",
                svc->service_instance_id);

    if (svc->is_load && (svc->load < 0 || svc->load > 100))
        return validation_fail(err, OGS_SBI_HTTP_STATUS_BAD_REQUEST,
                OGS_SBI_CAUSE_OPTIONAL_IE_INCORRECT,
                "Invalid NFProfile", "load",
                "load out of range in service [%s]",
                svc->service_instance_id);

    return validate_ip_end_points(
            svc->ip_end_points, svc->service_instance_id, err);
}

int ogs_nnrf_nfm_validate_nf_profile(
        const OpenAPI_nf_profile_t *profile,
        const char *uri_nf_instance_id,
        ogs_nnrf_profile_validation_t *err)
{
    OpenAPI_lnode_t *node;
    bool has_nf_addr = false;
    int n_svc = 0;
    int i, j;
    const char *seen_ids[OGS_NNRF_MAX_NF_SERVICES];
    int n_seen = 0;

    if (err)
        memset(err, 0, sizeof(*err));

    if (!profile)
        return validation_fail(err, OGS_SBI_HTTP_STATUS_BAD_REQUEST,
                OGS_SBI_CAUSE_MANDATORY_IE_MISSING,
                "Invalid NFProfile", "NFProfile",
                "No NFProfile");

    if (!profile->nf_instance_id || !profile->nf_instance_id[0])
        return validation_fail(err, OGS_SBI_HTTP_STATUS_BAD_REQUEST,
                OGS_SBI_CAUSE_MANDATORY_IE_MISSING,
                "Invalid NFProfile", "nfInstanceId",
                "Missing nfInstanceId");

    if (strlen(profile->nf_instance_id) > 64)
        return validation_fail(err, OGS_SBI_HTTP_STATUS_BAD_REQUEST,
                OGS_SBI_CAUSE_MANDATORY_IE_INCORRECT,
                "Invalid NFProfile", "nfInstanceId",
                "nfInstanceId too long");

    if (uri_nf_instance_id &&
            strcmp(uri_nf_instance_id, profile->nf_instance_id) != 0)
        return validation_fail(err, OGS_SBI_HTTP_STATUS_BAD_REQUEST,
                OGS_SBI_CAUSE_MANDATORY_IE_INCORRECT,
                "Invalid NFProfile", "nfInstanceId",
                "NF Instance ID in request body does not match resource URI");

    if (!profile->nf_type)
        return validation_fail(err, OGS_SBI_HTTP_STATUS_BAD_REQUEST,
                OGS_SBI_CAUSE_MANDATORY_IE_MISSING,
                "Invalid NFProfile", "nfType",
                "Missing nfType");

    if (!profile->nf_status)
        return validation_fail(err, OGS_SBI_HTTP_STATUS_BAD_REQUEST,
                OGS_SBI_CAUSE_MANDATORY_IE_MISSING,
                "Invalid NFProfile", "nfStatus",
                "Missing nfStatus");

    if (profile->is_heart_beat_timer && profile->heart_beat_timer <= 0)
        return validation_fail(err, OGS_SBI_HTTP_STATUS_BAD_REQUEST,
                OGS_SBI_CAUSE_MANDATORY_IE_INCORRECT,
                "Invalid NFProfile", "heartBeatTimer",
                "Invalid heartBeatTimer [%d]", profile->heart_beat_timer);

    if (profile->fqdn && profile->fqdn[0])
        has_nf_addr = true;

    if (profile->ipv4_addresses) {
        int n = 0;
        OpenAPI_list_for_each(profile->ipv4_addresses, node) {
            char *ip = node->data;
            n++;
            if (n > OGS_NNRF_MAX_NF_ADDRESSES)
                return validation_fail(err, OGS_SBI_HTTP_STATUS_BAD_REQUEST,
                        OGS_SBI_CAUSE_OPTIONAL_IE_INCORRECT,
                        "Invalid NFProfile", "ipv4Addresses",
                        "Too many ipv4Addresses");
            if (!ipv4_ok(ip))
                return validation_fail(err, OGS_SBI_HTTP_STATUS_BAD_REQUEST,
                        OGS_SBI_CAUSE_MANDATORY_IE_INCORRECT,
                        "Invalid NFProfile", "ipv4Addresses",
                        "Invalid IPv4 address [%s]", ip ? ip : "(null)");
            has_nf_addr = true;
        }
    }

    if (profile->ipv6_addresses) {
        int n = 0;
        OpenAPI_list_for_each(profile->ipv6_addresses, node) {
            char *ip = node->data;
            n++;
            if (n > OGS_NNRF_MAX_NF_ADDRESSES)
                return validation_fail(err, OGS_SBI_HTTP_STATUS_BAD_REQUEST,
                        OGS_SBI_CAUSE_OPTIONAL_IE_INCORRECT,
                        "Invalid NFProfile", "ipv6Addresses",
                        "Too many ipv6Addresses");
            if (!ipv6_ok(ip))
                return validation_fail(err, OGS_SBI_HTTP_STATUS_BAD_REQUEST,
                        OGS_SBI_CAUSE_MANDATORY_IE_INCORRECT,
                        "Invalid NFProfile", "ipv6Addresses",
                        "Invalid IPv6 address [%s]", ip ? ip : "(null)");
            has_nf_addr = true;
        }
    }

    if (!has_nf_addr)
        return validation_fail(err, OGS_SBI_HTTP_STATUS_BAD_REQUEST,
                OGS_SBI_CAUSE_MANDATORY_IE_MISSING,
                "Invalid NFProfile",
                "fqdn|ipv4Addresses|ipv6Addresses",
                "At least one NF-level addressing parameter is required");

    if (profile->is_priority &&
            (profile->priority < 0 || profile->priority > 65535))
        return validation_fail(err, OGS_SBI_HTTP_STATUS_BAD_REQUEST,
                OGS_SBI_CAUSE_OPTIONAL_IE_INCORRECT,
                "Invalid NFProfile", "priority",
                "priority out of range");

    if (profile->is_capacity &&
            (profile->capacity < 0 || profile->capacity > 65535))
        return validation_fail(err, OGS_SBI_HTTP_STATUS_BAD_REQUEST,
                OGS_SBI_CAUSE_OPTIONAL_IE_INCORRECT,
                "Invalid NFProfile", "capacity",
                "capacity out of range");

    if (profile->is_load && (profile->load < 0 || profile->load > 100))
        return validation_fail(err, OGS_SBI_HTTP_STATUS_BAD_REQUEST,
                OGS_SBI_CAUSE_OPTIONAL_IE_INCORRECT,
                "Invalid NFProfile", "load",
                "load out of range");

    /* nfServices array */
    OpenAPI_list_for_each(profile->nf_services, node) {
        OpenAPI_nf_service_t *svc = node->data;
        n_svc++;
        if (n_svc > OGS_NNRF_MAX_NF_SERVICES)
            return validation_fail(err, OGS_SBI_HTTP_STATUS_BAD_REQUEST,
                    OGS_SBI_CAUSE_OPTIONAL_IE_INCORRECT,
                    "Invalid NFProfile", "nfServices",
                    "Too many NFServices");
        if (validate_one_service(svc, NULL, err) != OGS_OK)
            return OGS_ERROR;
        if (svc && svc->service_instance_id) {
            for (i = 0; i < n_seen; i++) {
                if (!strcmp(seen_ids[i], svc->service_instance_id))
                    return validation_fail(err,
                            OGS_SBI_HTTP_STATUS_BAD_REQUEST,
                            OGS_SBI_CAUSE_MANDATORY_IE_INCORRECT,
                            "Invalid NFProfile", "serviceInstanceId",
                            "Duplicate serviceInstanceId [%s]",
                            svc->service_instance_id);
            }
            if (n_seen < OGS_NNRF_MAX_NF_SERVICES)
                seen_ids[n_seen++] = svc->service_instance_id;
        }
    }

    /* nfServiceList map */
    OpenAPI_list_for_each(profile->nf_service_list, node) {
        OpenAPI_map_t *map = node->data;
        OpenAPI_nf_service_t *svc;

        n_svc++;
        if (n_svc > OGS_NNRF_MAX_NF_SERVICES)
            return validation_fail(err, OGS_SBI_HTTP_STATUS_BAD_REQUEST,
                    OGS_SBI_CAUSE_OPTIONAL_IE_INCORRECT,
                    "Invalid NFProfile", "nfServiceList",
                    "Too many NFServices");

        if (!map)
            return validation_fail(err, OGS_SBI_HTTP_STATUS_BAD_REQUEST,
                    OGS_SBI_CAUSE_MANDATORY_IE_MISSING,
                    "Invalid NFProfile", "nfServiceList",
                    "Null nfServiceList map entry");

        svc = map->value;
        if (validate_one_service(svc, map->key, err) != OGS_OK)
            return OGS_ERROR;

        if (svc && svc->service_instance_id) {
            for (j = 0; j < n_seen; j++) {
                if (!strcmp(seen_ids[j], svc->service_instance_id))
                    return validation_fail(err,
                            OGS_SBI_HTTP_STATUS_BAD_REQUEST,
                            OGS_SBI_CAUSE_MANDATORY_IE_INCORRECT,
                            "Invalid NFProfile", "serviceInstanceId",
                            "Duplicate serviceInstanceId [%s]",
                            svc->service_instance_id);
            }
            if (n_seen < OGS_NNRF_MAX_NF_SERVICES)
                seen_ids[n_seen++] = svc->service_instance_id;
        }
    }

    return OGS_OK;
}
