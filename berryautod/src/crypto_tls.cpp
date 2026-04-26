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
    
    // 1. Force TLS 1.2
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
    SSL_CTX_set_max_proto_version(ctx, TLS1_2_VERSION);
    
    // 2. CRITICAL FIX: Lower OpenSSL 3.0 Security Level to 0 to allow Automotive Ciphers (e.g., CBC/SHA1)
    SSL_CTX_set_security_level(ctx, 0);

    // 3. Explicitly enable all compatible Automotive ciphers
    SSL_CTX_set_cipher_list(ctx, "ALL:!EXPORT:!LOW:!aNULL:!eNULL:!SSLv2");

    // Do not strictly verify the Head Unit's client certificate
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);

    if (!generate_ephemeral_cert()) {
        std::cerr << "[TLS-ERR] Failed to generate ephemeral certificate!" << std::endl;
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

bool OpenGALTlsContext::generate_ephemeral_cert() {
    std::cout << "[TLS] Generating ephemeral RSA-2048 keypair and certificate..." << std::endl;
    
    EVP_PKEY_CTX *ctx_key = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL);
    EVP_PKEY_keygen_init(ctx_key);
    EVP_PKEY_CTX_set_rsa_keygen_bits(ctx_key, 2048);
    EVP_PKEY *pkey = NULL;
    EVP_PKEY_keygen(ctx_key, &pkey);
    EVP_PKEY_CTX_free(ctx_key);

    X509 *x509 = X509_new();
    ASN1_INTEGER_set(X509_get_serialNumber(x509), 1);
    X509_gmtime_adj(X509_get_notBefore(x509), 0);
    X509_gmtime_adj(X509_get_notAfter(x509), 31536000L); // 1 year validity
    X509_set_pubkey(x509, pkey);

    X509_NAME *name = X509_get_subject_name(x509);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, (unsigned char *)"Android", -1, -1, 0);
    X509_set_issuer_name(x509, name);

    // Self-sign the certificate
    X509_sign(x509, pkey, EVP_sha256());

    SSL_CTX_use_certificate(ctx, x509);
    SSL_CTX_use_PrivateKey(ctx, pkey);

    bool success = SSL_CTX_check_private_key(ctx);
    if (!success) {
        std::cerr << "[TLS-ERR] Private key does not match the certificate!" << std::endl;
        ERR_print_errors_fp(stderr);
    }
    
    X509_free(x509);
    EVP_PKEY_free(pkey);
    
    return success;
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