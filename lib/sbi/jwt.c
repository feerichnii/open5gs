/*
 * Minimal JWT HS256 (lab OAuth2)
 */

#include "ogs-sbi.h"
#include "jwt.h"

#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>

static char *base64url_encode(const unsigned char *in, int inlen)
{
    BIO *b64 = NULL, *bmem = NULL;
    BUF_MEM *bptr = NULL;
    char *out = NULL;
    int i, olen = 0;

    b64 = BIO_new(BIO_f_base64());
    bmem = BIO_new(BIO_s_mem());
    ogs_assert(b64 && bmem);
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    b64 = BIO_push(b64, bmem);
    BIO_write(b64, in, inlen);
    (void)BIO_flush(b64);
    BIO_get_mem_ptr(b64, &bptr);
    ogs_assert(bptr && bptr->data);

    out = ogs_calloc(1, bptr->length + 1);
    ogs_assert(out);
    memcpy(out, bptr->data, bptr->length);
    for (i = 0; out[i]; i++) {
        if (out[i] == '+') out[i] = '-';
        else if (out[i] == '/') out[i] = '_';
    }
    while (olen < (int)bptr->length && out[bptr->length - 1 - olen] == '=')
        out[bptr->length - 1 - olen] = '\0', olen++;

    BIO_free_all(b64);
    return out;
}

static int base64url_decode(const char *in, unsigned char *out, int outlen)
{
    BIO *b64 = NULL, *bmem = NULL;
    char *norm = NULL;
    int len, pad, i, j = 0;

    len = strlen(in);
    norm = ogs_calloc(1, len + 4);
    ogs_assert(norm);
    for (i = 0; in[i]; i++) {
        if (in[i] == '-')
            norm[i] = '+';
        else if (in[i] == '_')
            norm[i] = '/';
        else
            norm[i] = in[i];
    }
    pad = (4 - (len % 4)) % 4;
    for (i = 0; i < pad; i++)
        norm[len + i] = '=';
    norm[len + pad] = '\0';

    b64 = BIO_new(BIO_f_base64());
    bmem = BIO_new_mem_buf(norm, -1);
    ogs_assert(b64 && bmem);
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    b64 = BIO_push(b64, bmem);
    j = BIO_read(b64, out, outlen);
    BIO_free_all(b64);
    ogs_free(norm);
    return j > 0 ? j : 0;
}

char *ogs_sbi_jwt_encode_hs256(const char *payload_json, const char *secret)
{
    char *header = NULL, *payload_b64 = NULL, *header_b64 = NULL;
    char *signing_input = NULL, *sig_b64 = NULL, *jwt = NULL;
    unsigned char sig[EVP_MAX_MD_SIZE];
    unsigned int siglen = 0;

    ogs_assert(payload_json);
    ogs_assert(secret);

    header = ogs_msprintf("{\"alg\":\"HS256\",\"typ\":\"JWT\"}");
    ogs_assert(header);
    header_b64 = base64url_encode((unsigned char *)header, strlen(header));
    payload_b64 = base64url_encode(
            (unsigned char *)payload_json, strlen(payload_json));
    ogs_free(header);
    ogs_assert(header_b64 && payload_b64);

    signing_input = ogs_msprintf("%s.%s", header_b64, payload_b64);
    ogs_assert(signing_input);

    HMAC(EVP_sha256(), secret, strlen(secret),
            (unsigned char *)signing_input, strlen(signing_input),
            sig, &siglen);
    sig_b64 = base64url_encode(sig, siglen);
    ogs_assert(sig_b64);

    jwt = ogs_msprintf("%s.%s", signing_input, sig_b64);
    ogs_free(signing_input);
    ogs_free(header_b64);
    ogs_free(payload_b64);
    ogs_free(sig_b64);
    return jwt;
}

bool ogs_sbi_jwt_verify_hs256(
        const char *jwt, const char *secret, char **payload_out)
{
    char *copy = NULL, *dot1 = NULL, *dot2 = NULL;
    char *signing_input = NULL, *sig_b64 = NULL;
    unsigned char sig[EVP_MAX_MD_SIZE], expected[EVP_MAX_MD_SIZE];
    unsigned int siglen = 0, expected_len = 0;
    unsigned char payload_buf[4096];
    int plen;

    ogs_assert(jwt);
    ogs_assert(secret);

    copy = ogs_strdup(jwt);
    ogs_assert(copy);
    dot1 = strchr(copy, '.');
    if (!dot1) goto fail;
    dot2 = strchr(dot1 + 1, '.');
    if (!dot2) goto fail;
    *dot2 = '\0';
    sig_b64 = dot2 + 1;
    signing_input = copy;

    HMAC(EVP_sha256(), secret, strlen(secret),
            (unsigned char *)signing_input, strlen(signing_input),
            expected, &expected_len);
    plen = base64url_decode(sig_b64, sig, sizeof(sig));
    if (plen <= 0 || (unsigned int)plen != expected_len ||
            memcmp(sig, expected, expected_len) != 0)
        goto fail;

    plen = base64url_decode(dot1 + 1, payload_buf, sizeof(payload_buf) - 1);
    if (plen <= 0) goto fail;
    payload_buf[plen] = '\0';
    if (payload_out)
        *payload_out = ogs_strdup((char *)payload_buf);
    ogs_free(copy);
    return true;

fail:
    ogs_free(copy);
    return false;
}
