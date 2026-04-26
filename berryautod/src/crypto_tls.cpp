#include "crypto_tls.h"
#include <openssl/evp.h>
#include <openssl/x509.h>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <openssl/sha.h>
#include <iostream>
#include <cstring>

OpenGALTlsContext::OpenGALTlsContext() {
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();

    ctx = SSL_CTX_new(TLS_server_method());
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
    SSL_CTX_set_max_proto_version(ctx, TLS1_2_VERSION);
    
    // As the Emitter (Server), we don't strictly verify the Head Unit's certificate in this implementation
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);

    if (!load_google_identity()) {
        std::cerr << "[TLS-ERR] Failed to load Google Identity! Connection will likely be dropped by Head Unit." << std::endl;
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

    // 288 byte AES-256 encrypted array from Section 4.4
    const uint8_t encrypted_rsa_key[288] = {
        (uint8_t)-32, (uint8_t)-79, (uint8_t)-4, (uint8_t)-83, (uint8_t)-4, 6, 64, 26, (uint8_t)-105, 40, (uint8_t)-65, 14, (uint8_t)-63, (uint8_t)-55, 88, 69,
        79, 106, 101, 49, 92, (uint8_t)-66, (uint8_t)-41, 77, (uint8_t)-6, 106, (uint8_t)-80, (uint8_t)-71, (uint8_t)-91, 92, 74, 80,
        (uint8_t)-114, (uint8_t)-80, (uint8_t)-12, 125, 104, (uint8_t)-54, (uint8_t)-33, 84, (uint8_t)-122, (uint8_t)-75, 24, (uint8_t)-39, 60, (uint8_t)-59, (uint8_t)-30, (uint8_t)-31,
        83, (uint8_t)-95, (uint8_t)-103, 98, (uint8_t)-92, 6, (uint8_t)-110, (uint8_t)-17, 67, 30, (uint8_t)-7, 120, 49, (uint8_t)-55, (uint8_t)-34, (uint8_t)-121,
        73, 90, (uint8_t)-52, (uint8_t)-126, (uint8_t)-8, (uint8_t)-120, (uint8_t)-8, (uint8_t)-15, (uint8_t)-118, 8, 118, 104, (uint8_t)-25, (uint8_t)-27, 76, 98,
        76, (uint8_t)-91, 19, (uint8_t)-38, 6, (uint8_t)-105, (uint8_t)-118, (uint8_t)-108, (uint8_t)-115, 44, (uint8_t)-40, (uint8_t)-54, 82, 73, (uint8_t)-8, 31,
        47, 10, (uint8_t)-2, 15, (uint8_t)-85, 87, 79, (uint8_t)-60, 50, (uint8_t)-36, (uint8_t)-14, (uint8_t)-24, 59, 0, 35, 34,
        (uint8_t)-103, (uint8_t)-22, 90, (uint8_t)-60, 23, (uint8_t)-40, 123, (uint8_t)-110, 33, 85, 101, 16, 102, (uint8_t)-100, 80, 98,
        (uint8_t)-66, 60, (uint8_t)-40, (uint8_t)-59, 45, (uint8_t)-25, 96, (uint8_t)-104, 63, 93, 114, (uint8_t)-88, 10, 116, (uint8_t)-20, 15,
        117, (uint8_t)-28, (uint8_t)-76, 9, (uint8_t)-113, (uint8_t)-13, (uint8_t)-101, 122, 68, (uint8_t)-24, (uint8_t)-70, 121, 71, 123, 6, 109,
        (uint8_t)-106, 106, 41, 1, (uint8_t)-63, (uint8_t)-30, 26, 33, 48, (uint8_t)-113, (uint8_t)-25, (uint8_t)-99, 44, 82, 13, 50,
        (uint8_t)-122, 120, (uint8_t)-101, (uint8_t)-85, 53, (uint8_t)-70, (uint8_t)-12, 16, (uint8_t)-27, 20, 1, 83, (uint8_t)-114, (uint8_t)-42, 72, 76,
        103, 95, 48, (uint8_t)-68, (uint8_t)-50, 105, 104, (uint8_t)-96, 2, (uint8_t)-16, (uint8_t)-113, 39, 31, (uint8_t)-17, 45, 127,
        (uint8_t)-6, (uint8_t)-34, (uint8_t)-27, 2, (uint8_t)-65, 40, (uint8_t)-77, (uint8_t)-91, (uint8_t)-28, 11, (uint8_t)-78, 119, 118, (uint8_t)-13, (uint8_t)-39, 101,
        47, (uint8_t)-74, (uint8_t)-80, (uint8_t)-68, 71, 110, 45, (uint8_t)-15, (uint8_t)-50, (uint8_t)-92, (uint8_t)-79, (uint8_t)-60, (uint8_t)-1, (uint8_t)-26, (uint8_t)-104, (uint8_t)-45,
        109, (uint8_t)-70, (uint8_t)-108, 50, (uint8_t)-73, 13, (uint8_t)-28, (uint8_t)-26, (uint8_t)-114, 11, 35, (uint8_t)-41, (uint8_t)-23, (uint8_t)-23, (uint8_t)-89, 46
    };

    // 1. KDF to derive AES Key and IV (Section 4.5)
    uint8_t K[48] = {0};
    std::string salt = "com.google.android.gms";
    std::memcpy(K, salt.c_str(), salt.length());

    for (int i = 0; i < 8; i++) {
        unsigned char hash[SHA_DIGEST_LENGTH];
        SHA1(K, 48, hash);
        std::memcpy(K, hash, SHA_DIGEST_LENGTH);
    }

    uint8_t aes_key[32];
    uint8_t aes_iv[16];
    std::memcpy(aes_key, K, 32);
    std::memcpy(aes_iv, K + 32, 16);

    // 2. Decrypt the RSA Private Key
    EVP_CIPHER_CTX *cipher_ctx = EVP_CIPHER_CTX_new();
    EVP_DecryptInit_ex(cipher_ctx, EVP_aes_256_cbc(), NULL, aes_key, aes_iv);

    uint8_t decrypted_key[512]; 
    int out_len1 = 0;
    int out_len2 = 0;

    if (!EVP_DecryptUpdate(cipher_ctx, decrypted_key, &out_len1, encrypted_rsa_key, sizeof(encrypted_rsa_key))) {
        std::cerr << "[TLS-ERR] AES DecryptUpdate failed!" << std::endl;
        EVP_CIPHER_CTX_free(cipher_ctx);
        return false;
    }

    if (!EVP_DecryptFinal_ex(cipher_ctx, decrypted_key + out_len1, &out_len2)) {
        std::cerr << "[TLS-ERR] AES DecryptFinal failed! Bad padding or corrupted key." << std::endl;
        EVP_CIPHER_CTX_free(cipher_ctx);
        return false;
    }
    
    EVP_CIPHER_CTX_free(cipher_ctx);
    int total_decrypted_len = out_len1 + out_len2;

    // 3. Load the Private Key into OpenSSL
    const unsigned char *p = decrypted_key;
    EVP_PKEY *pkey = d2i_AutoPrivateKey(NULL, &p, total_decrypted_len);
    if (!pkey) {
        std::cerr << "[TLS-ERR] Failed to parse decrypted RSA key into EVP_PKEY." << std::endl;
        return false;
    }

    if (SSL_CTX_use_PrivateKey(ctx, pkey) != 1) {
        std::cerr << "[TLS-ERR] Failed to set Private Key in SSL_CTX." << std::endl;
        EVP_PKEY_free(pkey);
        return false;
    }

    // 4. Load the X.509 Certificate into OpenSSL
    BIO *cert_bio = BIO_new_mem_buf(google_cert_pem, -1);
    X509 *cert = PEM_read_bio_X509(cert_bio, NULL, NULL, NULL);
    if (!cert) {
        std::cerr << "[TLS-ERR] Failed to parse Google X.509 Certificate." << std::endl;
        BIO_free(cert_bio);
        EVP_PKEY_free(pkey);
        return false;
    }

    if (SSL_CTX_use_certificate(ctx, cert) != 1) {
        std::cerr << "[TLS-ERR] Failed to set Certificate in SSL_CTX." << std::endl;
        X509_free(cert);
        BIO_free(cert_bio);
        EVP_PKEY_free(pkey);
        return false;
    }

    // 5. Verify the combination
    if (SSL_CTX_check_private_key(ctx) != 1) {
        std::cerr << "[TLS-ERR] Private key does not match the certificate!" << std::endl;
        X509_free(cert);
        BIO_free(cert_bio);
        EVP_PKEY_free(pkey);
        return false;
    }

    X509_free(cert);
    BIO_free(cert_bio);
    EVP_PKEY_free(pkey);

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
            ERR_print_errors_fp(stderr);
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