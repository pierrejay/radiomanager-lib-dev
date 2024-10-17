#ifndef SIMPLE_CHA2_H
#define SIMPLE_CHA2_H

#include <Arduino.h>
#include <ChaCha.h>
#include <vector>

using Bytes = std::vector<uint8_t>;

class SimpleCha2 {
public:

    SimpleCha2(const uint8_t* initialKey);

    void setKey(const uint8_t* newKey);
    void resetEncryptCounter();
    void resetDecryptCounter();
    uint32_t getEncryptCounter() const;
    uint32_t getDecryptCounter() const;

    Bytes encrypt(const uint8_t* plaintext, size_t plaintextLen);
    Bytes encrypt(const Bytes& plaintext);
    Bytes encrypt(const String& plaintext);

    Bytes decrypt(const uint8_t* ciphertext, size_t ciphertextLen);
    Bytes decrypt(const Bytes& ciphertext);
    String decryptToStr(const uint8_t* ciphertext, size_t ciphertextLen);
    String decryptToStr(const Bytes& ciphertext);


private:
    static const size_t KEY_SIZE = 32;
    static const size_t NONCE_SIZE = 12;
    static const size_t COUNTER_SIZE = 4;
    static const size_t IV_SIZE = NONCE_SIZE - COUNTER_SIZE;

    uint8_t key[KEY_SIZE];
    uint32_t encryptCounter;
    uint32_t decryptCounter;
    ChaCha chacha;

    void generateIV(uint8_t* iv);
    void createNonce(uint8_t* nonce, const uint8_t* iv, uint32_t counter);
    uint32_t extractCounter(const uint8_t* nonce);
};

#endif // SIMPLE_CHA2_H