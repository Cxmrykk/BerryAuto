#include "crypto_tls.h"
#include <openssl/evp.h>
#include <openssl/x509.h>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <iostream>
#include <cstring>

OpenGALTlsContext::OpenGALTlsContext() {
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();

    ctx = SSL_CTX_new(TLS_server_method());
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
    SSL_CTX_set_max_proto_version(ctx, TLS1_2_VERSION);
    
    // Lower Security Level to allow legacy Automotive Ciphers (CBC/SHA1)
    SSL_CTX_set_security_level(ctx, 0);
    SSL_CTX_set_cipher_list(ctx, "ALL:!EXPORT:!LOW:!aNULL:!eNULL:!SSLv2");
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);

    if (!load_google_identity()) {
        std::cerr << "[TLS-ERR] Failed to load identity! Connection will be dropped by Car." << std::endl;
    }

    read_bio = BIO_new(BIO_s_mem());
    write_bio = BIO_new(BIO_s_mem());
    ssl = SSL_new(ctx);
    SSL_set_bio(ssl, read_bio, write_bio);
    SSL_set_accept_state(ssl);
}

OpenGALTlsContext::~OpenGALTlsContext() {
    SSL_free(ssl); 
    SSL_CTX_free(ctx);
}

bool OpenGALTlsContext::load_google_identity() {
    std::cout << "[TLS] Loading Official Google Automotive Link Identity..." << std::endl;

    const char* google_cert_pem = 
        "-----BEGIN CERTIFICATE-----\n"
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

    // TODO: Paste the decrypted, extracted Android Auto RSA Private Key here
    const char* google_priv_key_pem = 
        "-----BEGIN RSA PRIVATE KEY-----\n"
        "PASTE_EXTRACTED_KEY_HERE\n"
        "-----END RSA PRIVATE KEY-----\n";

    // 1. Load Certificate
    BIO *cert_bio = BIO_new_mem_buf(google_cert_pem, -1);
    X509 *cert = PEM_read_bio_X509(cert_bio, NULL, NULL, NULL);
    if (!cert || SSL_CTX_use_certificate(ctx, cert) != 1) {
        std::cerr << "[TLS-ERR] Failed to load Certificate." << std::endl;
        return false;
    }

    // 2. Load Private Key
    BIO *key_bio = BIO_new_mem_buf(google_priv_key_pem, -1);
    EVP_PKEY *pkey = PEM_read_bio_PrivateKey(key_bio, NULL, NULL, NULL);
    if (!pkey || SSL_CTX_use_PrivateKey(ctx, pkey) != 1) {
        std::cerr << "[TLS-ERR] Failed to load Private Key. (Did you paste the extracted key?)" << std::endl;
        return false;
    }

    if (SSL_CTX_check_private_key(ctx) != 1) {
        std::cerr << "[TLS-ERR] Private key does not match the certificate!" << std::endl;
        return false;
    }

    X509_free(cert); BIO_free(cert_bio);
    EVP_PKEY_free(pkey); BIO_free(key_bio);

    std::cout << "[TLS] Official Identity loaded successfully." << std::endl;
    return true;
}

bool OpenGALTlsContext::do_handshake(const std::vector<uint8_t>& input_record, std::vector<uint8_t>& output_record) {
    if (!input_record.empty()) {
        BIO_write(read_bio, input_record.data(), input_record.size());
    }
    
    int ret = SSL_do_handshake(ssl);
    if (ret <= 0) {
        int err = SSL_get_error(ssl, ret);
        if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE) {
            std::cerr << "[TLS-ERR] Handshake error code: " << err << std::endl;
        }
    }

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
    if (read > 0) {
        plaintext.resize(read);
    } else {
        plaintext.clear();
    }
    return plaintext;
}