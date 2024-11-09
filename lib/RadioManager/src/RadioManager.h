#ifndef _RADIO_MANAGER_H_
#define _RADIO_MANAGER_H_

#include <RF24.h>
#include <Arduino.h>
#include <vector>
#include <mbedtls/ecdh.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <Base64.h>
#include <SimpleCha2.h>
#include <ArduinoJson.h>

// Ajoutez cette ligne
using Bytes = std::vector<uint8_t>;

class RadioManager {
public:
    static const uint8_t KEY_SIZE = 32;

    enum State {
        IDLE,
        TRANSMITTING,
        RECEIVING,
        PAIRING_LISTEN,
        PAIRING_TRANSMIT
    };

    struct PairedDevice {
        String addr;
        std::vector<Bytes> mailbox;
        uint8_t sharedKey[KEY_SIZE];
        uint8_t publicKey[KEY_SIZE];
        SimpleCha2 chaObject;

        PairedDevice() : chaObject(sharedKey) {}
    };

    // Utility functions
    RadioManager(uint8_t ce_pin, uint8_t csn_pin, const char* radio_id);
    bool begin();
    void loop();
    State getCurrentState();
    bool isBusy();
    bool isAvailable();
    String getRadioID();
    bool startPairing();
    void enable(bool en);

    // Message functions
    uint8_t isMsgAvailable(uint8_t channel);
    Bytes readMsg(uint8_t channel);
    void clearMessages(uint8_t channel);
    bool sendMsg(const Bytes& msg, uint8_t channel, uint8_t* status = nullptr, bool encryption = false);
    bool sendMsg(const String& msg, uint8_t channel, uint8_t* status = nullptr, bool encryption = false);
    bool sendMsgToAddr(const Bytes& msg, const String& targetAddr, uint8_t* status = nullptr, bool encryption = false);
    bool sendMsgToAddr(const String& msg, const String& targetAddr, uint8_t* status = nullptr, bool encryption = false);

    // Pairing functions
    static const uint8_t MAX_CHANNELS = 5;
    uint8_t getAvailableChannel();
    String getPairedAddr(uint8_t channel);
    String getPairedUID(uint8_t channel);
    uint8_t getPairedChannel(String& uid);
    bool setPairedAddr(String& address, uint8_t channel);
    bool setPairedAddr(String& address, uint8_t channel, uint8_t* publicKey);
    bool setPairedAddr(String& address, uint8_t channel, Bytes& publicKey);
    void clearPairedAddr(uint8_t channel);
    bool clearPairedUID(String& uid);

    // JSON import/export functions
    String getPairedDevicesJson(bool keys = true);
    bool setPairedDevicesJson(const String& addrJson);

    // Encryption functions
    bool setPairedDeviceKeys(uint8_t channel, const Bytes& publicKey);
    bool setPersonalKeys(const Bytes& publicKey, const Bytes& privateKey);
    void getPersonalKeys(Bytes& publicKey, Bytes& privateKey);
    bool generateX25519KeyPair(uint8_t* publicKey, uint8_t* privateKey);
    bool generateX25519SharedKey(const uint8_t* peerPublicKey, const uint8_t* privateKey, uint8_t* sharedKey);
    
    ~RadioManager(); // Destructor declaration

    // Configuration functions
    String exportCfg();
    bool importCfg(const String& jsonConfig);

private:

    // Utility functions
    bool checkValidAddr(String& addr);
    void pad(Bytes& payload, size_t paddingSize);
    void unpad(Bytes& payload);

    // Radio functions
    void initRadio();
    void handlePairing();
    void receiveData(uint8_t pipe_num);
    void sendData();

    // Encryption functions
    Bytes encryptMessage(uint8_t channel, const Bytes& message);
    Bytes decryptMessage(uint8_t channel, const Bytes& encryptedMessage);
    void setDevicePublicKey(uint8_t channel, const uint8_t* newPublicKey);
    void setDeviceSharedKey(uint8_t channel, const uint8_t* newSharedKey);

    // Radio comm variables
    bool isEnabled;
    RF24 radio;
    State currentState;
    String radioID;
    PairedDevice pairedDevices[MAX_CHANNELS];
    Bytes rxBuffer;
    static const uint8_t NRF_BUF_SIZE = 32;
    uint8_t txBuffer[NRF_BUF_SIZE];

    // Radio pairing variables
    unsigned long lastPairingAttempt;
    unsigned long pairingStartTime;
    uint8_t pairingAttempts;
    bool isUnpairReq;
    bool gotPubKey;
    bool sentPubKey;
    bool gotAck;
    bool sentAck;
    uint8_t tempPublicKey[32]; 
    uint8_t tempSharedKey[32];
    Bytes tempPayload;
    uint8_t pairingChannel;
    String pairingAddress;

    // Radio settings
    static const uint8_t CONFIG_CHANNEL = 109;
    static const uint8_t DATA_CHANNEL = 108;
    const unsigned long RECEIVE_TIMEOUT = 1000;
    static const unsigned long PAIRING_TIMEOUT = 10000;
    static const unsigned long PAIRING_INTERVAL = 250;
    static const unsigned long PAIRING_LISTEN_TIME = 5000;
    static const uint8_t PAIRING_ATTEMPTS = 3;
    static const uint16_t MAX_PACKET_SIZE = 32;

    // Message handling variables
    Bytes outgoingMsg;
    size_t outgoingMsgIndex;
    String outgoingTargetAddr;
    uint8_t* currentMsgStatus;

    // Message handling settings
    static const uint16_t MAX_MSG_SIZE = 2048; // cleartext 2048 bytes -> ciphertext 2060 bytes -> 72 fragments max (12-byte nonce, 3-byte headers)
    static const uint16_t MAX_PACKETS_RCV = 100; // ciphertext 2900 bytes (w/o headers) -> cleartext 2888 bytes max (12-byte nonce, 3-byte headers)
    static const uint8_t MAX_MAILBOX_MSG = 3; // 3 msg * 5 addresses * (2048+12) bytes + 3*12 bytes = ~31 KB max mailbox size

    // Message header structure & settings
    struct PacketHeader {
        uint8_t code;
        uint16_t index;
    } __attribute__((packed));
    static const uint8_t HEADER_SIZE = sizeof(PacketHeader);
    static const uint8_t START_CODE = 'M';
    static const uint8_t CONTINUE_CODE = 'C';

    // Encryption
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    uint8_t publicKey[KEY_SIZE];
    uint8_t privateKey[KEY_SIZE];
    SimpleCha2* tempCha;


};

#endif // RADIO_MANAGER_H
