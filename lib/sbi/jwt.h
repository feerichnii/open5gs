/*
 * Minimal JWT (HS256) for lab OAuth2 (E5)
 */

#if !defined(OGS_SBI_INSIDE) && !defined(OGS_SBI_COMPILATION)
#error "This header cannot be included directly."
#endif

#ifndef OGS_SBI_JWT_H
#define OGS_SBI_JWT_H

char *ogs_sbi_jwt_encode_hs256(const char *payload_json, const char *secret);
bool ogs_sbi_jwt_verify_hs256(
        const char *jwt, const char *secret, char **payload_out);

#endif /* OGS_SBI_JWT_H */
