#include "crypto_tls.h"
#include <openssl/evp.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <cstring>

const char* GOOGLE_CERT = "-----BEGIN CERTIFICATE-----\n"
"MIIDiTCCAnGgAwIBAgIJAMFO56WkVE1CMA0GCSqGSIb3DQEBBQUAMFsxCzAJBgNV\n"
"BAYTAlVTMRMwEQYDVQQIDApDYWxpZm9ybmlhMRYwFAYDVQQHDA1Nb3VudGFpbiBW\n"
"aWV3MR8wHQYDVQQKDBZHb29nbGUgQXV0b21vdGl2ZSBMaW5rMB4XDTE0MDYwNjE4\n"
"MjgxOVoXDTQ0MDYwNTE4MjgxOVowWzELMAkGA1UEBhMCVVMxEzARBgNVBAgMCkNh\n"
"bGlmb3JuaWExFjAUBgNVBAcMDU1vdW50YWluIFZpZXcxHzAdBgNVBAoMFkdvb2ds\n"
"ZSBBdXRvbW90aXZlIExpbmswggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIB\n"
"AQDUH+iIbwwVb74NdI5eBv/ACFmh4ml/NOW7gUVWdYX50n8uQQsHHLCNIhk5VV2H\n"
"hanvAZ/XXHPuVAPadE2HpnNqePKF/RDo4eJo/+rOief8gBYq/Z+OQTZeLdNm+GoI\n"
"HBrEjU4Ms8IdLuFW0jF8LlIRgekjLHpc7duUl3QpwBlmAWQK40T/SZjprlmhyqfJ\n"
"g1rxFdnGbrSibmCsTmb3m6WZyZUyrcwmd7t6q3pHbMABO+o02asPG/YPj/SJo4+i\n"
"fb5/Nk56f3hH9pBiPKQXJnVUdVLKMXSRgydDBsGSBol4C0JL77MNDrMR5jdafJ4j\n"
"mWmsa2+mnzoAv9AxEL9T0LiNAgMBAAGjUDBOMB0GA1UdDgQWBBS5dqvv8DPQiwrM\n"
"fgn8xKR91k7wgjAfBgNVHSMEGDAWgBS5dqvv8DPQiwrMfgn8xKR91k7wgjAMBgNV\n"
"HRMEBTADAQH/MA0GCSqGSIb3DQEBBQUAA4IBAQDKcnBsrbB0Jbz2VGJKP2lwYB6P\n"
"dCTCCpQu7dVp61UQOX+zWfd2hnNMnLs/r1xPO+eyN0vmw7sD05phaIhbXVauKWZi\n"
"9WqWHTaR+9s6CTyBOc1Mye0DMj+4vHt+WLmf0lYjkYUVYvR1EImX8ktXzkVmOqn+\n"
"e30siqlZ8pQpsOgegIKfJ+pNQM8c3eXVv3KFMUgjZW33SziZL8IMsLvSO+1LtH37\n"
"KqbTEMP6XUwVuZopgGvaHU74eT/WSRGlL7vX4OL5/UXXP4qsGH2Zp7uQlErv4H9j\n"
"kMs37UL1vGb4M8RM7Eyu9/RulepSmqZUF+3i+3eby8iGq/3OWk9wgJf7AXnx\n"
"-----END CERTIFICATE-----\n";

const int8_t encrypted_rsa_key[288] = {
    -32, -79, -4, -83, -4, 6, 64, 26, -105, 40, -65, 14, -63, -55, 88, 69,
    79, 106, 101, 49, 92, -66, -41, 77, -6, 106, -80, -71, -91, 92, 74, 80,
    -114, -80, -12, 125, 104, -54, -33, 84, -122, -75, 24, -39, 60, -59, -30, -31,
    83, -95, -103, 98, -92, 6, -110, -17, 67, 30, -7, 120, 49, -55, -34, -121,
    73, 90, -52, -126, -8, -120, -8, -15, -118, 8, 118, 104, -25, -27, 76, 98,
    76, -91, 19, -38, 6, -105, -118, -108, -115, 44, -40, -54, 82, 73, -8, 31,
    47, 10, -2, 15, -85, 87, 79, -60, 50, -36, -14, -24, 59, 0, 35, 34,
    -103, -22, 90, -60, 23, -40, 123, -110, 33, 85, 101, 16, 102, -100, 80, 98,
    -66, 60, -40, -59, 45, -25, 96, -104, 63, 93, 114, -88, 10, 116, -20, 15,
    117, -28, -76, 9, -113, -13, -101, 122, 68, -24, -70, 121, 71, 123, 6, 109,
    -106, 106, 41, 1, -63, -30, 26, 33, 48, -113, -25, -99, 44, 82, 13, 50,
    -122, 120, -101, -85, 53, -70, -12, 16, -27, 20, 1, 83, -114, -42, 72, 76,
    103, 95, 48, -68, -50, 105, 104, -96, 2, -16, -113, 39, 31, -17, 45, 127,
    -6, -34, -27, 2, -65, 40, -77, -91, -28, 11, -78, 119, 118, -13, -39, 101,
    47, -74, -80, -68, 71, 110, 45, -15, -50, -92, -79, -60, -1, -26, -104, -45,
    109, -70, -108, 50, -73, 13, -28, -26, -114, 11, 35, -41, -23, -23, -89, 46
};

EVP_PKEY* OpenGALTlsContext::decrypt_hardcoded_key() {
    uint8_t K[48] = {0};
    const char* salt = "com.google.android.gms";
    std::memcpy(K, salt, strlen(salt));

    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
    for (int i = 0; i < 8; ++i) {
        uint8_t hash[20]; unsigned int hash_len;
        EVP_DigestInit_ex(mdctx, EVP_sha1(), NULL);
        EVP_DigestUpdate(mdctx, K, sizeof(K));
        EVP_DigestFinal_ex(mdctx, hash, &hash_len);
        std::memcpy(K, hash, 20);
    }
    EVP_MD_CTX_free(mdctx);

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, K, K + 32);
    
    std::vector<uint8_t> plaintext(288 + EVP_MAX_BLOCK_LENGTH);
    int len1 = 0, len2 = 0;
    EVP_DecryptUpdate(ctx, plaintext.data(), &len1, (const uint8_t*)encrypted_rsa_key, 288);
    EVP_DecryptFinal_ex(ctx, plaintext.data() + len1, &len2);
    EVP_CIPHER_CTX_free(ctx);

    const uint8_t* p = plaintext.data();
    return d2i_AutoPrivateKey(NULL, &p, len1 + len2);
}

OpenGALTlsContext::OpenGALTlsContext() {
    SSL_library_init();
    ctx = SSL_CTX_new(TLS_server_method());
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
    SSL_CTX_set_max_proto_version(ctx, TLS1_2_VERSION);
    
    BIO* cert_bio = BIO_new_mem_buf(GOOGLE_CERT, -1);
    X509* cert = PEM_read_bio_X509(cert_bio, NULL, 0, NULL);
    SSL_CTX_use_certificate(ctx, cert);
    BIO_free(cert_bio);
    
    EVP_PKEY* pkey = decrypt_hardcoded_key();
    SSL_CTX_use_PrivateKey(ctx, pkey);
    EVP_PKEY_free(pkey);
    
    read_bio = BIO_new(BIO_s_mem());
    write_bio = BIO_new(BIO_s_mem());
    ssl = SSL_new(ctx);
    SSL_set_bio(ssl, read_bio, write_bio);
    SSL_set_accept_state(ssl);
}

OpenGALTlsContext::~OpenGALTlsContext() {
    SSL_free(ssl); SSL_CTX_free(ctx);
}

bool OpenGALTlsContext::do_handshake(const std::vector<uint8_t>& input_record, std::vector<uint8_t>& output_record) {
    if (!input_record.empty()) BIO_write(read_bio, input_record.data(), input_record.size());
    SSL_do_handshake(ssl);
    int pending = BIO_pending(write_bio);
    if (pending > 0) {
        output_record.resize(pending);
        BIO_read(write_bio, output_record.data(), pending);
    }
    return SSL_is_init_finished(ssl);
}

std::vector<uint8_t> OpenGALTlsContext::encrypt(const std::vector<uint8_t>& plaintext) {
    SSL_write(ssl, plaintext.data(), plaintext.size());
    int pending = BIO_pending(write_bio);
    std::vector<uint8_t> ciphertext(pending);
    BIO_read(write_bio, ciphertext.data(), pending);
    return ciphertext;
}

std::vector<uint8_t> OpenGALTlsContext::decrypt(const std::vector<uint8_t>& ciphertext) {
    BIO_write(read_bio, ciphertext.data(), ciphertext.size());
    std::vector<uint8_t> plaintext(ciphertext.size() + 16384);
    int read = SSL_read(ssl, plaintext.data(), plaintext.size());
    if (read > 0) plaintext.resize(read);
    else plaintext.clear();
    return plaintext;
}
