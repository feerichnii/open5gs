/*
 * Copyright (C) 2026 Open5GS Model D lab contributors
 *
 * NRF transparent NFProfile JSON (E14-01)
 */

#include "ogs-sbi.h"
#include "nnrf-profile-raw.h"

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

static bool supi_in_range_item(cJSON *range, const char *supi)
{
    cJSON *start = NULL, *end = NULL, *pattern = NULL;

    ogs_assert(supi);

    if (!range || !cJSON_IsObject(range))
        return false;

    pattern = cJSON_GetObjectItemCaseSensitive(range, "pattern");
    if (pattern && cJSON_IsString(pattern) && pattern->valuestring) {
        if (strstr(supi, pattern->valuestring))
            return true;
    }

    start = cJSON_GetObjectItemCaseSensitive(range, "start");
    end = cJSON_GetObjectItemCaseSensitive(range, "end");
    if (start && cJSON_IsString(start) && start->valuestring &&
        end && cJSON_IsString(end) && end->valuestring) {
        if (strcmp(supi, start->valuestring) >= 0 &&
            strcmp(supi, end->valuestring) <= 0)
            return true;
    } else if (start && cJSON_IsString(start) && start->valuestring) {
        if (strcmp(supi, start->valuestring) == 0)
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
    if (!ranges || !cJSON_IsArray(ranges))
        return false;

    cJSON_ArrayForEach(item, ranges) {
        if (supi_in_range_item(item, supi))
            return true;
    }

    return false;
}

static bool supi_in_info_list(cJSON *profile, const char *info_key, const char *supi)
{
    cJSON *info = NULL, *list = NULL, *item = NULL;

    info = cJSON_GetObjectItemCaseSensitive(profile, info_key);
    if (info && cJSON_IsObject(info))
        return supi_in_info_ranges(info, supi);

    list = cJSON_GetObjectItemCaseSensitive(profile, info_key);
    if (list && cJSON_IsArray(list)) {
        cJSON_ArrayForEach(item, list) {
            if (supi_in_info_ranges(item, supi))
                return true;
        }
    }

    return false;
}

bool ogs_sbi_raw_profile_match_supi(cJSON *profile, const char *supi)
{
    static const char *info_keys[] = {
        "pcfInfo", "udmInfo", "udrInfo", "ausfInfo", "chfInfo",
        "bsfInfo", "nssfInfo", "smfInfo", "amfInfo", NULL
    };
    int i;

    ogs_assert(profile);
    ogs_assert(supi);

    for (i = 0; info_keys[i]; i++) {
        if (supi_in_info_list(profile, info_keys[i], supi))
            return true;
    }

    /* chfInfo uses supiRangeList in some profiles */
    {
        cJSON *chf = cJSON_GetObjectItemCaseSensitive(profile, "chfInfo");
        cJSON *ranges = NULL, *item = NULL;

        if (chf && cJSON_IsObject(chf)) {
            ranges = cJSON_GetObjectItemCaseSensitive(chf, "supiRangeList");
            if (ranges && cJSON_IsArray(ranges)) {
                cJSON_ArrayForEach(item, ranges) {
                    if (supi_in_range_item(item, supi))
                        return true;
                }
            }
        }
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
        return false;

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
    cJSON *item = NULL;

    ogs_assert(profile);
    ogs_assert(locality);

    item = cJSON_GetObjectItemCaseSensitive(profile, "locality");
    if (!item || !cJSON_IsString(item) || !item->valuestring)
        return false;

    return strcmp(item->valuestring, locality) == 0;
}
