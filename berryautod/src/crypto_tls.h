#pragma once
#include <openssl/ssl.h>
#include <openssl/bio.h>
#include <vector>
#include <cstdint>

class OpenGALTlsContext {
public:
    OpenGALTlsContext();
    ~OpenGALTlsContext();
    bool do_handshake(const std::vector<uint8_t>& input_record, std::vector<uint8_t>& output_record);
    std::vector<uint8_t> encrypt(const std::vector<uint8_t>& plaintext);
    std::vector<uint8_t> decrypt(const std::vector<uint8_t>& ciphertext);
private:
    SSL_CTX* ctx; 
    SSL* ssl; 
    BIO *read_bio, *write_bio;
    
    // Generates a dynamic RSA keypair to satisfy the Head Unit
    bool generate_ephemeral_cert();
};