/*
 * Copyright (C) 2026 Open5GS Model D lab contributors
 *
 * Semantic NFProfile validation (TS 29.510) before NRF commit
 */

#if !defined(OGS_SBI_INSIDE) && !defined(OGS_SBI_COMPILATION)
#error "This header cannot be included directly."
#endif

#ifndef OGS_NNRF_PROFILE_VALIDATE_H
#define OGS_NNRF_PROFILE_VALIDATE_H

typedef struct ogs_nnrf_profile_validation_s {
    int status;                 /* HTTP status, typically 400 */
    const char *title;
    char detail[256];
    const char *cause;          /* OGS_SBI_CAUSE_* */
    const char *invalid_param;
} ogs_nnrf_profile_validation_t;

/*
 * Strict TS 29.510 semantic checks. Returns OGS_OK or OGS_ERROR.
 * Does not mutate NRF context. err may be NULL.
 */
int ogs_nnrf_nfm_validate_nf_profile(
        const OpenAPI_nf_profile_t *profile,
        const char *uri_nf_instance_id,
        ogs_nnrf_profile_validation_t *err);

#endif /* OGS_NNRF_PROFILE_VALIDATE_H */
