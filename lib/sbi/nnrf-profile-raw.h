/*
 * NRF NFProfile raw JSON storage (Model D / E14)
 */

#if !defined(OGS_SBI_INSIDE) && !defined(OGS_SBI_COMPILATION)
#error "This header cannot be included directly."
#endif

#ifndef OGS_NNRF_PROFILE_RAW_H
#define OGS_NNRF_PROFILE_RAW_H

#include "third-party/cjson/cJSON.h"

void ogs_sbi_nf_instance_set_raw_profile(
        ogs_sbi_nf_instance_t *nf_instance, cJSON *raw_profile);
void ogs_sbi_nf_instance_clear_raw_profile(ogs_sbi_nf_instance_t *nf_instance);

void ogs_sbi_raw_profile_apply_runtime(
        cJSON *profile, ogs_sbi_nf_instance_t *nf_instance);

OpenAPI_nf_profile_t *ogs_nnrf_nfm_build_nf_profile_from_instance(
        ogs_sbi_nf_instance_t *nf_instance,
        const OpenAPI_service_name_e service_name,
        ogs_sbi_discovery_option_t *discovery_option,
        bool service_map);

bool ogs_sbi_raw_profile_match_supi(cJSON *profile, const char *supi);
bool ogs_sbi_raw_profile_match_routing_indicator(
        cJSON *profile, const char *routing_indicator);
bool ogs_sbi_raw_profile_match_nf_set_id(cJSON *profile, const char *nf_set_id);
bool ogs_sbi_raw_profile_match_preferred_locality(
        cJSON *profile, const char *locality);

#endif /* OGS_NNRF_PROFILE_RAW_H */
