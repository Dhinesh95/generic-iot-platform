/*
 * Minimal mbedTLS 2.28 configuration for the IoT platform.
 * Enables only: AES-128, SHA-256, HMAC, CCM, Message Digest, Cipher.
 * All TLS, X.509, RSA, ECP, DES, Blowfish, ARC4, etc. are disabled.
 */

#ifndef MBEDTLS_CONFIG_H
#define MBEDTLS_CONFIG_H

/* Platform */
#define MBEDTLS_HAVE_ASM
#define MBEDTLS_NO_PLATFORM_ENTROPY
#undef MBEDTLS_HAVE_TIME

/* AES (required by CCM) */
#define MBEDTLS_AES_C

/* SHA-256 (required by HMAC) */
#define MBEDTLS_SHA256_C

/* Message Digest abstraction (required for mbedtls_md_hmac) */
#define MBEDTLS_MD_C

/* Cipher abstraction (required by CCM and md_wrap) */
#define MBEDTLS_CIPHER_C
#define MBEDTLS_CIPHER_MODE_CTR

/* CCM authenticated encryption */
#define MBEDTLS_CCM_C

/* OID (required by md.c) */
#define MBEDTLS_OID_C

/* Error strings */
#define MBEDTLS_ERROR_C

/* Platform utility */
#define MBEDTLS_PLATFORM_C

/* ========== DISABLE everything else ========== */

/* No DES */
#undef MBEDTLS_DES_C

/* No Blowfish */
#undef MBEDTLS_BLOWFISH_C

/* No ARC4 */
#undef MBEDTLS_ARC4_C

/* No Camellia */
#undef MBEDTLS_CAMELLIA_C

/* No Aria */
#undef MBEDTLS_ARIA_C

/* No SEED */
#undef MBEDTLS_SEED_C

/* No GCM (not needed) */
#undef MBEDTLS_GCM_C

/* No Chacha20/Poly1305 */
#undef MBEDTLS_CHACHA20_C
#undef MBEDTLS_CHACHAPOLY_C

/* No AESNI (hardware accel) */
#undef MBEDTLS_AESNI_C

/* No AESCE */
#undef MBEDTLS_AESCE_C

/* No Padlock */
#undef MBEDTLS_PADLOCK_C

/* No ChaCha (old naming) */
#undef MBEDTLS_CHACHA20_POLY1305_C

/* No SHA-1, MD5, SHA-512, SHA-3, RIPEMD (only SHA-256 needed) */
#undef MBEDTLS_SHA1_C
#undef MBEDTLS_MD5_C
#undef MBEDTLS_SHA512_C
#undef MBEDTLS_SHA3_C
#undef MBEDTLS_RIPEMD160_C

/* No public key, X.509, TLS */
#undef MBEDTLS_PK_C
#undef MBEDTLS_PK_PARSE_C
#undef MBEDTLS_PK_WRITE_C
#undef MBEDTLS_X509_CRT_PARSE_C
#undef MBEDTLS_X509_CRL_PARSE_C
#undef MBEDTLS_X509_CSR_PARSE_C
#undef MBEDTLS_SSL_TLS_C
#undef MBEDTLS_SSL_CLI_C
#undef MBEDTLS_SSL_SRV_C

/* No RSA, ECP, DH, ECDSA */
#undef MBEDTLS_RSA_C
#undef MBEDTLS_ECP_C
#undef MBEDTLS_DHM_C
#undef MBEDTLS_ECDSA_C
#undef MBEDTLS_ECDH_C

/* No ASN.1 write */
#undef MBEDTLS_ASN1_WRITE_C

/* No bignum */
#undef MBEDTLS_BIGNUM_C

/* No CMAC (not needed by our code directly) */
#undef MBEDTLS_CMAC_C

/* No PEM, Base64 */
#undef MBEDTLS_PEM_PARSE_C
#undef MBEDTLS_BASE64_C

/* No MD2, MD4 */
#undef MBEDTLS_MD2_C
#undef MBEDTLS_MD4_C

#endif /* MBEDTLS_CONFIG_H */
