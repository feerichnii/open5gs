/*
 * Copyright (C) 2026 Open5GS Model D lab contributors
 *
 * NRF transparent NFProfile JSON (E14-01)
 */

#include "ogs-sbi.h"
#include "nnrf-profile-raw.h"

#include <regex.h>

void ogs_sbi_nf_instance_clear_raw_profile(ogs_sbi_nf_instance_t *nf_instance)
{
    ogs_assert(nf_instance);

    if (nf_instance->raw_profile) {
        cJSON_Delete(nf_instance->raw_profile);
        nf_instance->raw_profile = NULL;
    }
}

void ogs_sbi_nf_instance_set_raw_profile(
        ogs_sbi_nf_instance_t *nf_instance, cJSON *raw_profile)
{
    ogs_assert(nf_instance);

    ogs_sbi_nf_instance_clear_raw_profile(nf_instance);
    nf_instance->raw_profile = raw_profile;
}

void ogs_sbi_raw_profile_apply_runtime(
        cJSON *profile, ogs_sbi_nf_instance_t *nf_instance)
{
    cJSON *item = NULL;

    ogs_assert(profile);
    ogs_assert(nf_instance);

    if (nf_instance->nf_status) {
        item = cJSON_GetObjectItem(profile, "nfStatus");
        if (item && cJSON_IsString(item)) {
            cJSON_SetValuestring(item,
                    OpenAPI_nf_status_ToString(nf_instance->nf_status));
        }
    }

    item = cJSON_GetObjectItem(profile, "load");
    if (item && cJSON_IsNumber(item))
        cJSON_SetIntValue(item, nf_instance->load);
    else if (nf_instance->load) {
        cJSON_AddNumberToObject(profile, "load", nf_instance->load);
    }

    item = cJSON_GetObjectItem(profile, "priority");
    if (item && cJSON_IsNumber(item))
        cJSON_SetIntValue(item, nf_instance->priority);

    item = cJSON_GetObjectItem(profile, "capacity");
    if (item && cJSON_IsNumber(item))
        cJSON_SetIntValue(item, nf_instance->capacity);
}

OpenAPI_nf_profile_t *ogs_nnrf_nfm_build_nf_profile_from_instance(
        ogs_sbi_nf_instance_t *nf_instance,
        const OpenAPI_service_name_e service_name,
        ogs_sbi_discovery_option_t *discovery_option,
        bool service_map)
{
    OpenAPI_nf_profile_t *NFProfile = NULL;
    cJSON *dup = NULL;

    ogs_assert(nf_instance);

    if (nf_instance->raw_profile) {
        dup = cJSON_Duplicate(nf_instance->raw_profile, 1);
        ogs_assert(dup);
        ogs_sbi_raw_profile_apply_runtime(dup, nf_instance);
        NFProfile = OpenAPI_nf_profile_parseFromJSON(dup);
        cJSON_Delete(dup);
        if (NFProfile)
            return NFProfile;
        ogs_warn("[%s] raw NFProfile parse failed, fallback to rebuild",
                nf_instance->id);
    }

    return ogs_nnrf_nfm_build_nf_profile_legacy(
            nf_instance, service_name, discovery_option, service_map);
}

/* TS 29.571 SupiRange.start/end are digit strings without "imsi-" prefix */
static const char *supi_digits(const char *supi)
{
    if (!supi)
        return NULL;
    if (!ogs_strncasecmp(supi, "imsi-", 5))
        return supi + 5;
    return supi;
}

static bool digistr_in_range(const char *digits,
        const char *start, const char *end)
{
    size_t dlen, slen, elen;

    ogs_assert(digits);
    if (!start)
        return false;

    dlen = strlen(digits);
    slen = strlen(start);

    if (!end || !end[0])
        return strcmp(digits, start) == 0;

    elen = strlen(end);
    if (dlen < slen || dlen > elen)
        return false;
    if (dlen == slen && strcmp(digits, start) < 0)
        return false;
    if (dlen == elen && strcmp(digits, end) > 0)
        return false;
    return true;
}

static bool supi_in_range_item(cJSON *range, const char *supi)
{
    cJSON *start = NULL, *end = NULL, *pattern = NULL;
    const char *digits = NULL;
    regex_t re;
    int rc;

    ogs_assert(supi);

    if (!range || !cJSON_IsObject(range))
        return false;

    digits = supi_digits(supi);
    ogs_assert(digits);

    pattern = cJSON_GetObjectItemCaseSensitive(range, "pattern");
    if (pattern && cJSON_IsString(pattern) && pattern->valuestring) {
        if (regcomp(&re, pattern->valuestring, REG_EXTENDED | REG_NOSUB) == 0) {
            rc = regexec(&re, digits, 0, NULL, 0);
            if (rc != 0)
                rc = regexec(&re, supi, 0, NULL, 0);
            regfree(&re);
            if (rc == 0)
                return true;
        }
    }

    start = cJSON_GetObjectItemCaseSensitive(range, "start");
    end = cJSON_GetObjectItemCaseSensitive(range, "end");
    if (start && cJSON_IsString(start) && start->valuestring) {
        const char *s = start->valuestring;
        const char *e = (end && cJSON_IsString(end) && end->valuestring) ?
            end->valuestring : NULL;
        if (digistr_in_range(digits, s, e))
            return true;
    }

    return false;
}

static bool supi_in_info_ranges(cJSON *info, const char *supi)
{
    cJSON *ranges = NULL;
    cJSON *item = NULL;

    if (!info || !cJSON_IsObject(info))
        return false;

    ranges = cJSON_GetObjectItemCaseSensitive(info, "supiRanges");
    if (!ranges || !cJSON_IsArray(ranges)) {
        ranges = cJSON_GetObjectItemCaseSensitive(info, "supiRangeList");
        if (!ranges || !cJSON_IsArray(ranges))
            return false;
    }

    cJSON_ArrayForEach(item, ranges) {
        if (supi_in_range_item(item, supi))
            return true;
    }

    return false;
}

static bool info_has_supi_ranges(cJSON *info)
{
    cJSON *ranges;

    if (!info || !cJSON_IsObject(info))
        return false;
    ranges = cJSON_GetObjectItemCaseSensitive(info, "supiRanges");
    if (ranges && cJSON_IsArray(ranges) && cJSON_GetArraySize(ranges) > 0)
        return true;
    ranges = cJSON_GetObjectItemCaseSensitive(info, "supiRangeList");
    if (ranges && cJSON_IsArray(ranges) && cJSON_GetArraySize(ranges) > 0)
        return true;
    return false;
}

static bool profile_has_any_supi_ranges(cJSON *profile)
{
    static const char *info_keys[] = {
        "pcfInfo", "udmInfo", "udrInfo", "ausfInfo", "chfInfo",
        "bsfInfo", "nssfInfo", "smfInfo", NULL
    };
    int i;
    cJSON *info;

    for (i = 0; info_keys[i]; i++) {
        info = cJSON_GetObjectItemCaseSensitive(profile, info_keys[i]);
        if (info_has_supi_ranges(info))
            return true;
    }
    return false;
}

bool ogs_sbi_raw_profile_match_supi(cJSON *profile, const char *supi)
{
    static const char *info_keys[] = {
        "pcfInfo", "udmInfo", "udrInfo", "ausfInfo", "chfInfo",
        "bsfInfo", "nssfInfo", "smfInfo", NULL
    };
    int i;

    ogs_assert(profile);
    ogs_assert(supi);

    /* No ranges advertised → do not filter out */
    if (!profile_has_any_supi_ranges(profile))
        return true;

    for (i = 0; info_keys[i]; i++) {
        cJSON *info = cJSON_GetObjectItemCaseSensitive(profile, info_keys[i]);
        if (supi_in_info_ranges(info, supi))
            return true;
    }

    return false;
}

static bool routing_in_info(cJSON *info, const char *routing_indicator)
{
    cJSON *indicators = NULL;
    cJSON *item = NULL;

    if (!info || !cJSON_IsObject(info) || !routing_indicator)
        return false;

    indicators = cJSON_GetObjectItemCaseSensitive(info, "routingIndicators");
    if (!indicators || !cJSON_IsArray(indicators))
        return false;

    cJSON_ArrayForEach(item, indicators) {
        if (cJSON_IsString(item) && item->valuestring &&
            strcmp(item->valuestring, routing_indicator) == 0)
            return true;
    }

    return false;
}

bool ogs_sbi_raw_profile_match_routing_indicator(
        cJSON *profile, const char *routing_indicator)
{
    cJSON *udm = NULL, *ausf = NULL;

    ogs_assert(profile);
    ogs_assert(routing_indicator);

    udm = cJSON_GetObjectItemCaseSensitive(profile, "udmInfo");
    if (routing_in_info(udm, routing_indicator))
        return true;

    ausf = cJSON_GetObjectItemCaseSensitive(profile, "ausfInfo");
    if (routing_in_info(ausf, routing_indicator))
        return true;

    /* No routingIndicators advertised → accept */
    if ((!udm || !cJSON_GetObjectItemCaseSensitive(udm, "routingIndicators")) &&
        (!ausf || !cJSON_GetObjectItemCaseSensitive(ausf, "routingIndicators")))
        return true;

    return false;
}

bool ogs_sbi_raw_profile_match_nf_set_id(cJSON *profile, const char *nf_set_id)
{
    cJSON *list = NULL;
    cJSON *item = NULL;

    ogs_assert(profile);
    ogs_assert(nf_set_id);

    list = cJSON_GetObjectItemCaseSensitive(profile, "nfSetIdList");
    if (!list || !cJSON_IsArray(list))
        return true; /* no set advertised */

    cJSON_ArrayForEach(item, list) {
        if (cJSON_IsString(item) && item->valuestring &&
            strcmp(item->valuestring, nf_set_id) == 0)
            return true;
    }

    return false;
}

bool ogs_sbi_raw_profile_match_preferred_locality(
        cJSON *profile, const char *locality)
{
    /* preferred-locality is a preference (sort), not a hard filter */
    (void)profile;
    (void)locality;
    return true;
}

int ogs_sbi_raw_profile_locality_score(
        cJSON *profile, const char *preferred_locality)
{
    cJSON *item;

    if (!preferred_locality || !profile)
        return 0;

    item = cJSON_GetObjectItemCaseSensitive(profile, "locality");
    if (item && cJSON_IsString(item) && item->valuestring &&
            strcmp(item->valuestring, preferred_locality) == 0)
        return 1;
    return 0;
}
