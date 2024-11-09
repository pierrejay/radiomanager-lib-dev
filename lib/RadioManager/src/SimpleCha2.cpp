#include "SimpleCha2.h"
#include <esp_system.h>
#include <Base64.h>

/**
 * @brief Construct a new SimpleCha2 object
 * 
 * @param initialKey Pointer to the initial key (32 bytes)
 */
SimpleCha2::SimpleCha2(const uint8_t* initialKey) : encryptCounter(0), decryptCounter(0) {
    setKey(initialKey);
}

/**
 * @brief Set a new key for encryption/decryption
 * 
 * @param newKey Pointer to the new key (32 bytes)
 */
void SimpleCha2::setKey(const uint8_t* newKey) {
    memcpy(key, newKey, KEY_SIZE);
    resetEncryptCounter();
    resetDecryptCounter();
}

/**
 * @brief Encrypt a byte array
 * 
 * @param plaintext Pointer to the plaintext
 * @param plaintextLen Length of the plaintext
 * @return vector Encrypted data (nonce + ciphertext)
 */
Bytes SimpleCha2::encrypt(const uint8_t* plaintext, size_t plaintextLen) {
    uint8_t iv[IV_SIZE];
    generateIV(iv);
    uint8_t nonce[NONCE_SIZE];
    createNonce(nonce, iv, ++encryptCounter);

    Bytes ciphertext(plaintextLen);

    chacha.setKey(key, KEY_SIZE);
    chacha.setIV(nonce, NONCE_SIZE);
    chacha.encrypt(ciphertext.data(), plaintext, plaintextLen);

    Bytes combined(NONCE_SIZE + ciphertext.size());
    memcpy(combined.data(), nonce, NONCE_SIZE);
    memcpy(combined.data() + NONCE_SIZE, ciphertext.data(), ciphertext.size());

    return combined;
}

/**
 * @brief Encrypt a vector
 * 
 * @param plaintext Pointer to the plaintext
 * @return vector Encrypted data (nonce + ciphertext)
 */
Bytes SimpleCha2::encrypt(const Bytes& plaintext) {
    return encrypt(plaintext.data(), plaintext.size());
}



/**
 * @brief Encrypt a string
 * 
 * @param plaintext String to encrypt
 * @return vector Encrypted data (nonce + ciphertext)
 */
Bytes SimpleCha2::encrypt(const String& plaintext) {
    return encrypt(reinterpret_cast<const uint8_t*>(plaintext.c_str()), plaintext.length());
}


/**
 * @brief Decrypt a byte array
 * 
 * @param ciphertext Pointer to the ciphertext (including nonce)
 * @param ciphertextLen Length of the ciphertext (including nonce)
 * @return vector Decrypted data
 */
Bytes SimpleCha2::decrypt(const uint8_t* ciphertext, size_t ciphertextLen) {
    if (ciphertextLen < NONCE_SIZE) {
        return Bytes();
    }

    uint8_t nonce[NONCE_SIZE];
    memcpy(nonce, ciphertext, NONCE_SIZE);

    uint32_t receivedCounter = extractCounter(nonce);
    if (receivedCounter <= decryptCounter) {
        return Bytes();
    }
    decryptCounter = receivedCounter;

    size_t dataSize = ciphertextLen - NONCE_SIZE;
    Bytes decrypted(dataSize);

    chacha.setKey(key, KEY_SIZE);
    chacha.setIV(nonce, NONCE_SIZE);
    chacha.decrypt(decrypted.data(), ciphertext + NONCE_SIZE, dataSize);

    return decrypted;
}

/**
 * @brief Decrypt a vector
 * 
 * @param ciphertext Pointer to the ciphertext
 * @return vector Decrypted data
 */
Bytes SimpleCha2::decrypt(const Bytes& ciphertext) {
    return decrypt(ciphertext.data(), ciphertext.size());
}


/**
 * @brief Decrypt a byte array to a String
 * 
 * @param ciphertext Pointer to the ciphertext (including nonce)
 * @param ciphertextLen Length of the ciphertext (including nonce)
 * @return String Decrypted string
 */
String SimpleCha2::decryptToStr(const uint8_t* ciphertext, size_t ciphertextLen) {
    Bytes decrypted = decrypt(ciphertext, ciphertextLen);
    if (decrypted == Bytes()) return "";
    return String(reinterpret_cast<const char*>(decrypted.data()), decrypted.size());
}

/**
 * @brief Decrypt a vector to a String
 * 
 * @param ciphertext Vector containing the ciphertext (including nonce)
 * @return String Decrypted string
 */
String SimpleCha2::decryptToStr(const Bytes& ciphertext) {
    return decryptToStr(ciphertext.data(), ciphertext.size());
}

/**
 * @brief Reset the encryption counter
 */
void SimpleCha2::resetEncryptCounter() {
    encryptCounter = 0;
}

/**
 * @brief Reset the decryption counter
 */
void SimpleCha2::resetDecryptCounter() {
    decryptCounter = 0;
}

/**
 * @brief Get the current encryption counter value
 * 
 * @return uint32_t Current encryption counter
 */
uint32_t SimpleCha2::getEncryptCounter() const {
    return encryptCounter;
}

/**
 * @brief Get the current decryption counter value
 * 
 * @return uint32_t Current decryption counter
 */
uint32_t SimpleCha2::getDecryptCounter() const {
    return decryptCounter;
}


void SimpleCha2::generateIV(uint8_t* iv) {
    esp_fill_random(iv, IV_SIZE);
}


void SimpleCha2::createNonce(uint8_t* nonce, const uint8_t* iv, uint32_t counter) {
    memcpy(nonce, iv, IV_SIZE);
    memcpy(nonce + IV_SIZE, &counter, COUNTER_SIZE);
}

uint32_t SimpleCha2::extractCounter(const uint8_t* nonce) {
    uint32_t counter;
    memcpy(&counter, nonce + IV_SIZE, COUNTER_SIZE);
    return counter;
}