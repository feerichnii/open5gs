/*
 * SBI OAuth2 lab helpers (E5)
 */

#if !defined(OGS_SBI_INSIDE) && !defined(OGS_SBI_COMPILATION)
#error "This header cannot be included directly."
#endif

#ifndef OGS_SBI_OAUTH_H
#define OGS_SBI_OAUTH_H

bool ogs_sbi_oauth_server_authorize(
        ogs_sbi_stream_t *stream, ogs_sbi_request_t *request);
char *ogs_sbi_oauth_issue_access_token(
        const char *nf_instance_id, OpenAPI_nf_type_e nf_type,
        OpenAPI_nf_type_e target_nf_type, const char *scope);

#endif /* OGS_SBI_OAUTH_H */
