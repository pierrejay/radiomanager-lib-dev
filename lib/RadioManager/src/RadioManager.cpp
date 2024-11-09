#include <Arduino.h>
#include "RadioManager.h"
#include <mbedtls/ecdh.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <Base64.h>
#include <SimpleCha2.h>
#include <ArduinoJson.h>

// #define RADIO_MANAGER_DEBUG // Uncomment to enable serial logs

#ifdef RADIO_MANAGER_DEBUG
    #define LOG_(x) Serial.print(x)
    #define LOG_LN(x) Serial.println(x)
#else
    #define LOG_(x)
    #define LOG_LN(x)
#endif

using Bytes = std::vector<uint8_t>;

/**
 * @brief Constructor for RadioManager
 * 
 * @param ce_pin CE pin for the radio module
 * @param csn_pin CSN pin for the radio module
 * @param radio_id Unique identifier for this radio (will be trimmed to 4 characters)
 */
RadioManager::RadioManager(uint8_t ce_pin, uint8_t csn_pin, const char* radio_id)
    : radio(ce_pin, csn_pin), currentState(IDLE),
      lastPairingAttempt(0), pairingStartTime(0), pairingAttempts(0), tempCha(nullptr), isEnabled(false) {

    // Adjust radio_id to ensure it's exactly 4 characters
    String tempID = String(radio_id);
    if (tempID.length() < 4) {
        // Pad with zeros if less than 4 characters
        while (tempID.length() < 4) {
            tempID = "0" + tempID;
        }
    } else if (tempID.length() > 4) {
        // Truncate to first 4 characters if longer
        tempID = tempID.substring(0, 4);
    }
    radioID = tempID;

    // Initialize pairedDevices
    for (int i = 0; i < MAX_CHANNELS; i++) {
        pairedDevices[i].addr = String("");
        pairedDevices[i].mailbox.clear();
        memset(pairedDevices[i].sharedKey, 0, sizeof(pairedDevices[i].sharedKey));
        memset(pairedDevices[i].publicKey, 0, sizeof(pairedDevices[i].publicKey));
    }

    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    
    // Use radioID as part of the personalization string
    String pers = String("radio_manager_") + radioID;
    if(mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                             (const unsigned char *)pers.c_str(), pers.length()) != 0) {
        // Handle initialization error here
    }

    // Generate X25519 key pair
    if (!generateX25519KeyPair(publicKey, privateKey)) {
        // Handle key generation error here
        LOG_LN("Failed to generate X25519 key pair");
    }
}

/**
 * @brief Initializes the radio module and configures initial parameters
 * 
 * @return true if initialization was successful, false otherwise
 */
bool RadioManager::begin() {
    if (!radio.begin()) {
        LOG_LN("Radio init error!");
        isEnabled = false;
        return false;
    }
    
    isEnabled = true;
    radio.setPALevel(RF24_PA_MAX, true);
    radio.setDataRate(RF24_250KBPS);
    radio.setChannel(DATA_CHANNEL);
    
    for (int i = 0; i < MAX_CHANNELS; i++) {
        if (!pairedDevices[i].addr.isEmpty()) {
            radio.openReadingPipe(i + 1, (uint8_t*)(String(i) + radioID).c_str());
        }
    }
    
    radio.startListening();
    return true;
}

/**
 * @brief Main function to be called frequently in the program's main loop
 * Manages the different states of the RadioManager
 */
void RadioManager::loop() {
    if (!isEnabled) {
        return;  // Do nothing if RadioManager is disabled
    }

    switch (currentState) {
        case PAIRING_LISTEN:
        case PAIRING_TRANSMIT:
            handlePairing();
            break;
        case IDLE:
            {
                delete tempCha;
                tempCha = nullptr;
                uint8_t pipe_num;
                if (radio.available(&pipe_num)) {
                    currentState = RECEIVING;
                    receiveData(pipe_num);
                    LOG_("Radio Packet Received on Pipe ");
                    LOG_LN(pipe_num);
                }
            }
            break;
        case TRANSMITTING:
            sendData();
            break;
        case RECEIVING:
            // Do nothing here, as receiveData is called in the IDLE case
            break;
    }
}

/**
 * @brief Gets the current state of the RadioManager
 * 
 * @return The current state of the RadioManager
 */
RadioManager::State RadioManager::getCurrentState() {
    return currentState;
}

/**
 * @brief Checks if the RadioManager is busy (pairing, transmitting or receiving)
 * 
 * @return true if the RadioManager is busy, false otherwise
 */
bool RadioManager::isBusy() {
    return currentState == PAIRING_LISTEN || 
           currentState == PAIRING_TRANSMIT || 
           currentState == TRANSMITTING || 
           currentState == RECEIVING;
}

/**
 * @brief Checks if the RadioManager is available for operations
 * 
 * @return true if the RadioManager is available, false otherwise
 */
bool RadioManager::isAvailable() {
    return !isBusy();
}

/**
 * @brief Checks the number of messages available on a specific channel
 * 
 * @param channel The channel number to check
 * @return The number of messages available on the specified channel
 */
uint8_t RadioManager::isMsgAvailable(uint8_t channel) {
    if (channel >= 0 && channel < MAX_CHANNELS && !pairedDevices[channel].addr.isEmpty()) {
        return pairedDevices[channel].mailbox.size();
    }
    return 0;
}

/**
 * @brief Reads an available message on a specific channel
 * 
 * @param channel The channel number to read from
 * @return The read message as a vector of uint8_t, or an empty vector if no message is available
 */
Bytes RadioManager::readMsg(uint8_t channel) {
    if (channel >= 0 && channel < MAX_CHANNELS && !pairedDevices[channel].addr.isEmpty() && !pairedDevices[channel].mailbox.empty()) {
        Bytes msg = pairedDevices[channel].mailbox.front();
        pairedDevices[channel].mailbox.erase(pairedDevices[channel].mailbox.begin());
        LOG_("Message read from mailbox ");
        LOG_LN(channel);
        return msg;
    }
    return Bytes();
}

/**
 * @brief Sends a message on a specific channel
 * 
 * @param msg The message to send
 * @param channel The channel number on which to send the message
 * @param status Pointer to a variable to track the sending progress (optional) : 0 = in progress, -1 = error, 1 = successful
 * @return true if the sending was successful, false otherwise
 */
bool RadioManager::sendMsg(const Bytes& msg, uint8_t channel, uint8_t* status, bool encryption) {
    if (!isEnabled) {
        if (status) *status = -1;
        return false;  // Do not send message if RadioManager is disabled
    }

    if (channel < 0 || channel >= MAX_CHANNELS || pairedDevices[channel].addr.isEmpty()) {
        if (status) *status = -1;
        return false;  // Invalid or unpaired channel
    }
    return sendMsgToAddr(msg, pairedDevices[channel].addr, status, encryption);
}

bool RadioManager::sendMsg(const String& msg, uint8_t channel, uint8_t* status, bool encryption) {
    if (!isEnabled) {
        if (status) *status = -1;
        return false;  // Do not send message if RadioManager is disabled
    }

    Bytes msgBytes(msg.begin(), msg.end());
    return sendMsg(msgBytes, channel, status, encryption);
}

/**
 * @brief Sends a message to a specific device identified by its Addr
 * 
 * @param msg The message to send
 * @param targetAddr The Addr of the target device
 * @param status Pointer to a variable to track the sending progress (optional) : 0 = in progress, -1 = error, 1 = successful
 * @param encryption Whether to encrypt the message (default: false)
 * @return true if the sending was successful, false otherwise
 */
bool RadioManager::sendMsgToAddr(const Bytes& msg, const String& targetAddr, uint8_t* status, bool encryption) {
    if (!isEnabled) {
        if (status) *status = -1;
        return false;  // Do not send message if RadioManager is disabled
    }

    if (currentState != IDLE || msg.size() > MAX_MSG_SIZE) {
        if (status) *status = -1;
        return false;
    }

    currentState = TRANSMITTING;

    // Prepare the message for sending
    outgoingMsg.clear();

    if (encryption) {
        // Find the channel for the target address
        int targetChannel = -1;
        for (int i = 0; i < MAX_CHANNELS; i++) {
            if (pairedDevices[i].addr == targetAddr) {
                targetChannel = i;
                break;
            }
        }

        if (targetChannel != -1) {
            outgoingMsg = encryptMessage(targetChannel, msg);
            LOG_LN("Encrypted message (Base64): " + Base64::encode(outgoingMsg.data(), outgoingMsg.size()));
        } else {
            LOG_LN("Warning: Target address not found for encryption. Sending unencrypted.");
            outgoingMsg = msg;
        }
    } else {
        outgoingMsg = msg;
    }

    outgoingMsgIndex = 0;
    outgoingTargetAddr = targetAddr;
    currentMsgStatus = status;

    if (status) *status = 0;  // Initialize status to "in progress"

    radio.stopListening();
    radio.openWritingPipe((uint8_t*)targetAddr.c_str());

    // Start sending
    sendData();
    LOG_("Start Sending Message to Address ");
    LOG_LN(targetAddr);
    LOG_LN("Raw message (Base64): " + Base64::encode(msg.data(), msg.size()));

    return true;
}

bool RadioManager::sendMsgToAddr(const String& msg, const String& targetAddr, uint8_t* status, bool encryption) {
    if (!isEnabled) {
        if (status) *status = -1;
        return false;  // Do not send message if RadioManager is disabled
    }

    Bytes msgBytes(msg.begin(), msg.end());
    return sendMsgToAddr(msgBytes, targetAddr, status, encryption);
}

/**
 * @brief Gets the Addr of the paired device on a specific channel
 * 
 * @param channel The channel number
 * @return The Addr of the paired device, or an empty string if no device is paired on this channel
 */
String RadioManager::getPairedAddr(uint8_t channel) {
    if (channel >= 0 && channel < MAX_CHANNELS) {
        return pairedDevices[channel].addr;
    }
    return "";
}

/**
 * @brief Gets the UID of the paired device on a specific channel
 * 
 * @param channel The channel number
 * @return The UID of the paired device, or an empty string if no device is paired on this channel
 */
String RadioManager::getPairedUID(uint8_t channel) {
    if (channel >= 0 && channel < MAX_CHANNELS) {
        return pairedDevices[channel].addr.substring(1,5);
    }
    return "";
}

/**
 * @brief Gets the pairing channel of a given UID (if it is paired)
 * 
 * @param  uid The searched 4-character UID
 * @return The pairing channel of the searched UID, or 255 if UID was not found
 */
uint8_t RadioManager::getPairedChannel(String& uid) {
    uint8_t ret = 255;
    for (int i=0 ; i<MAX_CHANNELS ; i++) {
        String testUID = getPairedUID(i);
        if (testUID == uid) {
            ret = i;
            break;
        }
    }
    return ret;
}

/**
 * @brief Sets the Addr of a paired device on a specific channel
 * 
 * @param addr The Addr to set
 * @param channel The channel number
 * @return true if the operation was successful, false otherwise
 */
bool RadioManager::setPairedAddr(String& address, uint8_t channel) {
    return setPairedAddr(address, channel, nullptr);
}

/**
 * @brief Sets the Addr of a paired device on a specific channel including encryption keys
 * 
 * @param addr The Addr to set
 * @param channel The channel number
 * @param publicKey Pointer to public key (32 bytes)
 * @return true if the operation was successful, false otherwise
 */
bool RadioManager::setPairedAddr(String& address, uint8_t channel, uint8_t* publicKey) {
    if (channel >= 0 && channel < MAX_CHANNELS) {
        bool hasKey = (publicKey != nullptr);
        uint8_t sharedKey[KEY_SIZE];
        if (hasKey) {
            bool keyGen = generateX25519SharedKey(publicKey, privateKey, sharedKey);
            if (!keyGen) return false;
        }
        clearPairedAddr(channel);
        pairedDevices[channel].addr = address;
        if (hasKey) {
            setDevicePublicKey(channel, publicKey);
            setDeviceSharedKey(channel, sharedKey);
        }
        radio.openReadingPipe(channel + 1, (uint8_t*)(String(channel) + radioID).c_str());
        return true;
    }
    return false;
}

/**
 * @brief Sets the Addr of a paired device on a specific channel including encryption keys
 * 
 * @param addr The Addr to set
 * @param channel The channel number
 * @param publicKey Reference to Bytes of public key
 * @return true if the operation was successful, false otherwise
 */
bool RadioManager::setPairedAddr(String& address, uint8_t channel, Bytes& publicKey) {
    if (publicKey.size() != KEY_SIZE) return false;
    return setPairedAddr(address, channel, publicKey.data());
}

/**
 * @brief Clears the Addr of a paired device on a specific channel
 * 
 * @param channel The channel number to clear
 */
void RadioManager::clearPairedAddr(uint8_t channel) {
    if (channel >= 0 && channel < MAX_CHANNELS) {
        pairedDevices[channel].addr = String("");
        pairedDevices[channel].mailbox.clear();
        memset(pairedDevices[channel].sharedKey, 0, sizeof(pairedDevices[channel].sharedKey));
        memset(pairedDevices[channel].publicKey, 0, sizeof(pairedDevices[channel].publicKey));
        // Reset the chaObject with zeroed sharedKey
        pairedDevices[channel].chaObject.setKey(pairedDevices[channel].sharedKey);
    }
}

/**
 * @brief Gets the first available channel for pairing
 * 
 * @return The number of the first available channel, or 255 if no channel is available
 */
uint8_t RadioManager::getAvailableChannel() {
    for (int i = 0; i < MAX_CHANNELS; i++) {
        if (pairedDevices[i].addr.isEmpty()) {
            return i;
        }
    }
    return 255;  // No channel available (using 255 instead of 0 as an invalid value)
}

/**
 * @brief Initializes the radio module parameters
 */
void RadioManager::initRadio() {
    radio.setChannel(DATA_CHANNEL);
    
    // Open all reading pipes
    for (int i = 0; i < MAX_CHANNELS; i++) {
        radio.openReadingPipe(i + 1, (uint8_t*)(String(i+1) + radioID).c_str());
    }
    
    radio.startListening();
}

/**
 * @brief Starts the pairing process
 * 
 * @return true if the pairing process could be started, false otherwise
 */
bool RadioManager::startPairing() {
    if (!isEnabled) {
        return false;  // Do not start pairing if RadioManager is disabled
    }

    if (currentState == IDLE) {
        currentState = PAIRING_LISTEN;
        pairingStartTime = millis();
        isUnpairReq = false;
        memset(tempPublicKey, 0, sizeof(tempPublicKey));
        memset(tempSharedKey, 0, sizeof(tempSharedKey));
        tempPayload = Bytes();
        gotPubKey = false;
        sentPubKey = false;
        gotAck = false;
        sentAck = false;
        pairingChannel = getAvailableChannel();
        radio.setChannel(DATA_CHANNEL);
        radio.openReadingPipe(1, (uint8_t*)"CFGTX"); 
        radio.startListening();
        tempCha = new SimpleCha2(tempSharedKey);

        return true;
    }
    return false;
}

/**
 * @brief Handles the async pairing procedure
 */
void RadioManager::handlePairing() {
    unsigned long currentTime = millis();

    switch (currentState) {
        case PAIRING_LISTEN:
            // STEP 1: WAIT FOR PUB KEY
            if (radio.available() && !gotPubKey) {
                uint8_t receivedData[KEY_SIZE];
                radio.read(receivedData, sizeof(receivedData));
                memcpy(tempPublicKey, receivedData, sizeof(receivedData));
                String asciiPubKey = Base64::encode(tempPublicKey, sizeof(tempPublicKey));
                LOG_LN("L1: Received Public Key " + asciiPubKey);
                gotPubKey = true;
                // Generate Shared Secret
                generateX25519SharedKey(tempPublicKey, privateKey, tempSharedKey);
                String asciiSharedKey = Base64::encode(tempSharedKey, sizeof(tempSharedKey));
                tempCha->setKey(tempSharedKey);
                LOG_LN("L1: Generated Shared Key " + asciiSharedKey);
            } 

            // STEP 2: SEND PUB KEY, WAIT FOR PAIRING ADDRESS
            if (gotPubKey && !sentPubKey && (currentTime - lastPairingAttempt) > PAIRING_INTERVAL) {
                lastPairingAttempt = currentTime;
                radio.stopListening();
                radio.openWritingPipe((uint8_t*)"CFGRX");
                String asciiPubKey = Base64::encode(publicKey, sizeof(publicKey));
                if (radio.write(publicKey, sizeof(publicKey))) { 
                    LOG_LN("L2: Sent Public Key " + asciiPubKey + " OK");
                    sentPubKey = true;
                }
                else { 
                    LOG_LN("L2: Sent Public Key " + asciiPubKey + " unreceived"); 
                }
                radio.openReadingPipe(1, (uint8_t*)"CFGTX");
                radio.startListening();
            }

            // STEP 3: WAIT FOR PAIRING ADDRESS, DECRYPT AND CHECK VALIDITY
            if (sentPubKey && !gotAck && radio.available()) {
                // Wait for ACK return and check validity
                uint8_t packetSize = radio.getPayloadSize();
                Bytes packet(packetSize);
                LOG_LN("RECEIVED PACKET SIZE: "+String(packetSize));
                radio.read(packet.data(), packetSize);
                unpad(packet);
                String cipheredAck = Base64::encode(packet);
                LOG_LN("L3: Received Ciphered Ack " + cipheredAck);
                String receivedAddr = tempCha->decryptToStr(packet);
                LOG_LN("L3: Unciphered Ack = " + receivedAddr);
                if (checkValidAddr(receivedAddr)) {
                    gotAck = true;
                    // Extract UID and pipe num
                    String receivedUID = receivedAddr.substring(1, 5);
                    String receivedPipe = receivedAddr.substring(0, 1);
                    // Check if UID exists in database and must be unpaired
                    if (clearPairedUID(receivedUID)) {
                        LOG_LN("L3: Address " + receivedAddr + " successfully unpaired.");
                        isUnpairReq = true;
                    }
                    // If received unknown addr starting with 0, exit pairing
                    else if (receivedPipe == "0") {
                        LOG_LN("L3: Received invalid Unpair request from unknown Address " + receivedAddr + ", pairing aborted.");
                        currentState = IDLE;
                        initRadio();
                        return;
                    }
                    // Otherwise, pair the received address on the available channel if we have room
                    else if (pairingChannel < MAX_CHANNELS) {
                        setPairedAddr(receivedAddr, pairingChannel, tempPublicKey);
                        LOG_LN("L3: Received Valid ACK from Address " + receivedAddr);
                        LOG_LN("L3: Paired on Channel " + String(pairingChannel));
                    }
                    // All channels are occupied, we abort pairing
                    else {
                        LOG_LN("L3: All channels occupied, pairing aborted...");
                        currentState = IDLE;
                        initRadio();
                        return;
                    }
                }
                else {
                    LOG_LN("L3: Received invalid Ack...");
                }
            }

            // STEP 4: SEND ACK
            if (gotAck && !sentAck) {
                lastPairingAttempt = currentTime;
                radio.stopListening();
                radio.openWritingPipe((uint8_t*)"CFGRX");
                uint8_t pipeID = isUnpairReq ? 0 : (pairingChannel + 1);
                String pairingID = String(pipeID) + radioID;
                tempPayload = tempCha->encrypt(pairingID);
                LOG_LN("L4: Unciphered pairing address = " + pairingID);
                LOG_LN("L4: Ciphered pairing address = " + Base64::encode(tempPayload));
                pad(tempPayload, MAX_PACKET_SIZE);
                if (radio.write(tempPayload.data(), tempPayload.size())) { 
                    LOG_LN("L4: Sent ciphered pairing address OK, pairing successful.");
                    sentAck = true;
                    currentState = IDLE;
                    initRadio();
                    return;
                }
                else { 
                    LOG_LN("L4: Sent ciphered pairing address unreceived"); 
                }
                radio.openReadingPipe(1, (uint8_t*)"CFGTX");
                radio.startListening();
            }
            
            // If we exceed the pairing listen time before getting a valid pairing request, we switch to pairing transmit mode
            if (!gotPubKey && (currentTime - pairingStartTime > PAIRING_LISTEN_TIME)) {
                LOG_LN("Switching to Pairing Transmit Mode...");
                currentState = PAIRING_TRANSMIT;
                radio.stopListening();
                radio.openWritingPipe((uint8_t*)"CFGTX");
                pairingStartTime = currentTime;
            }
            break;

        case PAIRING_TRANSMIT:
            // STEP 1: SEND PUBLIC KEY
            if (!sentPubKey && (currentTime - lastPairingAttempt) > PAIRING_INTERVAL) {
                lastPairingAttempt = currentTime;
                radio.stopListening();
                radio.openWritingPipe((uint8_t*)"CFGTX");
                String asciiPubKey = Base64::encode(publicKey, sizeof(publicKey));
                if (radio.write(publicKey, sizeof(publicKey))) { 
                    LOG_LN("T1: Sent Public Key " + asciiPubKey + " OK");
                    sentPubKey = true;
                }
                else { 
                    LOG_LN("T1: Sent Public Key " + asciiPubKey + " unreceived"); 
                }
                radio.openReadingPipe(1, (uint8_t*)"CFGRX");
                radio.startListening();
            }
            
            // STEP 2: WAIT FOR PEER PUBLIC KEY
            if (sentPubKey && !gotPubKey && radio.available()) {
                uint8_t receivedData[KEY_SIZE];
                radio.read(receivedData, sizeof(receivedData));
                memcpy(tempPublicKey, receivedData, sizeof(receivedData));
                String asciiPubKey = Base64::encode(tempPublicKey, sizeof(tempPublicKey));
                LOG_LN("T2: Received Public Key " + asciiPubKey);
                gotPubKey = true;

                // Generate Shared Secret
                generateX25519SharedKey(tempPublicKey, privateKey, tempSharedKey);
                String asciiSharedKey = Base64::encode(tempSharedKey, sizeof(tempSharedKey));
                tempCha->setKey(tempSharedKey);
                LOG_LN("T2: Generated Shared Key " + asciiSharedKey);

                // Compute and encrypt pairing address
                uint8_t pipeID;
                if (pairingChannel >= MAX_CHANNELS) { 
                    isUnpairReq = true; 
                    pipeID = 0; 
                    LOG_LN("T2: Sending Unpair request...");
                }
                else if (pairingChannel < MAX_CHANNELS) pipeID = pairingChannel + 1;
                String pairingID = String(pipeID) + radioID;
                tempPayload = tempCha->encrypt(pairingID);
                LOG_LN("T2: Unciphered pairing address = " + pairingID);
                LOG_LN("T2: Ciphered pairing address = " + Base64::encode(tempPayload));
            }

            // STEP 3: SEND ENCRYPTED PAIRING ADDRESS AND WAIT FOR ACKNOWLEDGEMENT
            if (gotPubKey && !sentAck && (currentTime - lastPairingAttempt) > PAIRING_INTERVAL) {
                lastPairingAttempt = currentTime;
                // Send ciphered pairing address in Hex format
                radio.stopListening();
                radio.openWritingPipe((uint8_t*)"CFGTX");
                pad(tempPayload, MAX_PACKET_SIZE);
                if (radio.write(tempPayload.data(), tempPayload.size())) { 
                    LOG_LN("T3: Sent ciphered pairing address OK");
                    sentAck = true;
                }
                else { 
                    LOG_LN("T3: Sent ciphered pairing address, unreceived"); 
                }
                radio.openReadingPipe(1, (uint8_t*)"CFGRX");
                radio.startListening();
            }

            // STEP 4: DECRYPT ACK, CHECK VALIDITY AND COMPLETE PAIRING
            if (sentAck && !gotAck && radio.available()) {
                // Wait for ACK return and check validity
                uint8_t packetSize = radio.getPayloadSize();
                Bytes packet(packetSize);
                radio.read(packet.data(), packetSize);
                unpad(packet);
                String cipheredAck = Base64::encode(packet);
                LOG_LN("T4: Received Ciphered Ack " + cipheredAck);
                String receivedAddr = tempCha->decryptToStr(packet);
                LOG_LN("T4: Unciphered Ack = " + receivedAddr);
                if (checkValidAddr(receivedAddr)) {
                    gotAck = true;
                    // Extract UID and pipe num
                    String receivedUID = receivedAddr.substring(1, 5);
                    String receivedPipe = receivedAddr.substring(0, 1);
                    // If address starting by 0, try to unpair
                    if (receivedPipe == "0") {
                        if (clearPairedUID(receivedUID)) {
                            LOG_("T4: Received valid Unpair ACK from Address ");
                            LOG_(receivedAddr);
                            LOG_LN(", pairing successful.");
                        }
                        else {
                            LOG_("T4: Received invalid Unpair ACK from Address ");
                            LOG_(receivedAddr);
                            LOG_LN(", pairing aborted.");
                        }
                        currentState = IDLE;
                        initRadio();
                        return;
                    }
                    // Unpair request with invalid response
                    else if (isUnpairReq) {
                        LOG_LN("T4: Received invalid ACK to Unpair request from Address " + receivedAddr + ", pairing aborted");
                        currentState = IDLE;
                        initRadio();
                        return;
                    }
                    // Otherwise, pair the received address on the available channel
                    else if (!isUnpairReq) {
                        setPairedAddr(receivedAddr, pairingChannel, tempPublicKey);
                        LOG_LN("T4: Received Valid ACK from Address " + receivedAddr);
                        LOG_LN("T4: Paired on Channel " + String(pairingChannel));
                        LOG_LN("T4: Pairing success!");
                        currentState = IDLE;
                        initRadio();
                        return;
                    }
                }
                else {
                    LOG_LN("T4: Received invalid Ack...");
                }
            }
            break;

        default:
            break;
    }
    // If we exceed the pairing timeout, we abort pairing
    if (currentTime - pairingStartTime > PAIRING_TIMEOUT) {
        LOG_LN("Pairing Timeout, Returning Idle...");
        currentState = IDLE;
        initRadio();
        return;
    }
}

/**
 * @brief Sends the data
 */
void RadioManager::sendData() {
    const uint16_t PAYLOAD_SIZE = MAX_PACKET_SIZE - HEADER_SIZE;
    size_t msgSize = outgoingMsg.size();
    size_t totalFragments = (msgSize + PAYLOAD_SIZE - 1) / PAYLOAD_SIZE; // Calculate total fragments

    if (outgoingMsgIndex < msgSize) {
        size_t remainingSize = msgSize - outgoingMsgIndex;
        size_t packetSize = std::min<size_t>(PAYLOAD_SIZE, remainingSize);
        
        Bytes packet(MAX_PACKET_SIZE);
        PacketHeader header;
        
        // Prepare the header
        if (outgoingMsgIndex == 0) {
            header.code = START_CODE;
            header.index = totalFragments - 1; // Start with total fragments - 1
        } else {
            header.code = CONTINUE_CODE;
            header.index = (remainingSize <= PAYLOAD_SIZE) ? 0 : (totalFragments - 1 - outgoingMsgIndex / PAYLOAD_SIZE);
        }
        
        // Copy header and data
        memcpy(packet.data(), &header, HEADER_SIZE);
        memcpy(packet.data() + HEADER_SIZE, &outgoingMsg[outgoingMsgIndex], packetSize);

        // Pad the packet to 32 bits
        pad(packet, MAX_PACKET_SIZE);
        
        if (!radio.write(packet.data(), HEADER_SIZE + packetSize)) {
            // Sending failed, we reset
            currentState = IDLE;
            radio.startListening();
            if (currentMsgStatus) *currentMsgStatus = -1;  // Sending aborted with error
            LOG_LN("Failed to Send Radio Packet...");
            return;
        }

        outgoingMsgIndex += packetSize;

        // If we've sent the entire message, we finish
        if (outgoingMsgIndex >= msgSize) {
            currentState = IDLE;
            radio.startListening();
            if (currentMsgStatus) *currentMsgStatus = 1;  // Message sent successfully
            LOG_("Radio Packet Sent to ");
            LOG_LN(outgoingTargetAddr);
        }
        // Otherwise, we let the function end and it will be called again in the next loop()
    }
}

/**
 * @brief Receives data on a specific channel
 * 
 * @param pipe_num The channel number on which to receive data
 */
void RadioManager::receiveData(uint8_t pipe_num) {
    if (!isEnabled) {
        return;  // Do not receive data if RadioManager is disabled
    }

    static unsigned long lastReceiveTime = 0;
    static uint16_t expectedFragments = 0;
    static uint16_t receivedFragments = 0;
    uint8_t channel = pipe_num - 1;  // Convert pipe number to channel index

    uint8_t packetSize = radio.getPayloadSize();
    
    if (packetSize >= HEADER_SIZE && packetSize <= NRF_BUF_SIZE) {
        Bytes packet(packetSize);
        radio.read(packet.data(), packetSize);
        unpad(packet);
        
        PacketHeader header;
        memcpy(&header, packet.data(), HEADER_SIZE);
        
        if (header.code == START_CODE) {
            // New message, clear everything that came before
            rxBuffer.clear();
            expectedFragments = header.index + 1; // Set expected fragments
            receivedFragments = 0;
        }
        
        // Add the fragment to the buffer
        if (receivedFragments < MAX_PACKETS_RCV) {
            rxBuffer.insert(rxBuffer.end(), packet.begin() + HEADER_SIZE, packet.end());
            lastReceiveTime = millis();
            receivedFragments++;
        }
        
        // Check if it's the last fragment
        if (header.index == 0) {
            if (receivedFragments == expectedFragments) {
                // Process the complete message
                if (!pairedDevices[channel].addr.isEmpty()) {
                    LOG_LN("Received message (Base64): " + Base64::encode(rxBuffer.data(), rxBuffer.size()));

                    // Attempt to decrypt the message
                    Bytes decryptedData = decryptMessage(channel, rxBuffer);
                    
                    Bytes messageToStore;
                    if (!decryptedData.empty()) {
                        messageToStore = decryptedData;
                        LOG_LN("Decrypted message!");
                    } else {
                        messageToStore = rxBuffer;
                        LOG_LN("Message not decrypted (possibly unencrypted)");
                    }
                    LOG_LN("Decrypted message (Base64): " + Base64::encode(messageToStore.data(), messageToStore.size()));
                    LOG_LN("Decrypted message (Str): " + String(messageToStore.data(), messageToStore.size()));

                    if (pairedDevices[channel].mailbox.size() < MAX_MAILBOX_MSG) {
                        pairedDevices[channel].mailbox.push_back(messageToStore);
                    } else {
                        pairedDevices[channel].mailbox.erase(pairedDevices[channel].mailbox.begin());
                        pairedDevices[channel].mailbox.push_back(messageToStore);
                    }
                }
            } else {
                LOG_LN("Error: Incomplete message received. Expected " + String(expectedFragments) + " fragments, got " + String(receivedFragments));
            }
            
            // Reset the buffer and counters
            rxBuffer.clear();
            expectedFragments = 0;
            receivedFragments = 0;
        }
    }
    
    // Check if a partial message has expired
    if (!rxBuffer.empty() && millis() - lastReceiveTime > RECEIVE_TIMEOUT) {
        LOG_LN("Error: Message reception timeout. Clearing buffer.");
        rxBuffer.clear();
        expectedFragments = 0;
        receivedFragments = 0;
    }
    
    currentState = IDLE;
}

/**
 * @brief Generates an X25519 key pair
 * 
 * @return true if generation was successful, false otherwise
 */
bool RadioManager::generateX25519KeyPair(uint8_t* publicKey, uint8_t* privateKey) {
    mbedtls_ecdh_context ctx;
    mbedtls_ecdh_init(&ctx);

    int ret = mbedtls_ecp_group_load(&ctx.grp, MBEDTLS_ECP_DP_CURVE25519);
    if (ret != 0) {
        mbedtls_ecdh_free(&ctx);
        return false;
    }

    ret = mbedtls_ecdh_gen_public(&ctx.grp, &ctx.d, &ctx.Q,
                                  mbedtls_ctr_drbg_random, &ctr_drbg);
    if (ret != 0) {
        mbedtls_ecdh_free(&ctx);
        return false;
    }

    size_t olen;
    ret = mbedtls_ecp_point_write_binary(&ctx.grp, &ctx.Q, MBEDTLS_ECP_PF_COMPRESSED,
                                         &olen, publicKey, KEY_SIZE);
    if (ret != 0) {
        mbedtls_ecdh_free(&ctx);
        return false;
    }

    ret = mbedtls_mpi_write_binary(&ctx.d, privateKey, KEY_SIZE);
    if (ret != 0) {
        mbedtls_ecdh_free(&ctx);
        return false;
    }

    mbedtls_ecdh_free(&ctx);
    return true;
}

/**
 * @brief Destructor for RadioManager
 * Frees allocated resources
 */
RadioManager::~RadioManager() {
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);

    if (tempCha != nullptr) {
        delete tempCha;
    }
}

/**
 * @brief Gets the list of paired Addrs & keys as a string
 * 
 * @return A string containing the list of paired Addrs & keys, as a JSON object. 
 *         "0" represents an unpaired channel.
 */
String RadioManager::getPairedDevicesJson(bool keys) {
    String addrList;
    JsonDocument doc;
    for (int i = 0; i < MAX_CHANNELS; i++) {
        if (pairedDevices[i].addr.isEmpty()) {
            doc["addr"][i] = "0";
        } else {
            doc["addr"][i] = pairedDevices[i].addr;
            if (keys) {
                doc["pubKey"][i] = Base64::encode(pairedDevices[i].publicKey, KEY_SIZE);
            }
        }
    }
    serializeJson(doc, addrList);
    return addrList;
}

/**
 * @brief Sets the list of paired Addrs from a string
 * 
 * @param addrJson A JSON string containing the list of Addrs & pubKeys to set, separated by commas.
 *                "0" represents an unpaired channel.
 * @return true if the operation was successful, false otherwise
 */
bool RadioManager::setPairedDevicesJson(const String& addrJson) {
    JsonDocument doc;
    deserializeJson(doc, addrJson);

    if (!doc["addr"].is<JsonArray>()) {
        return false;
    }

    for (int i = 0; i < MAX_CHANNELS; i++) {
        if (doc["addr"][i].isNull()) continue;
        if (doc["addr"][i] == "0") {
            clearPairedAddr(i);
        } else {
            String addr = doc["addr"][i].as<String>();
            String pubKey = doc["pubKey"][i].as<String>();
            Bytes pubKeyBytes = Base64::decode(pubKey);
            setPairedAddr(addr, i, pubKeyBytes);
        }
    }

    // Reinitialize the radio to apply the new pairing configuration
    initRadio();

    return true;
}

/**
 * @brief Set personal public & private key (used for shared secret gen)
 * 
 * @param publicKey Reference to Bytes containing pub key
 * @param privateKey Reference to Bytes containing private key
 * 
 * @return True if keys set, false if invalid key(s)
 * 
 */
bool RadioManager::setPersonalKeys(const Bytes& publicKey, const Bytes& privateKey) {
    if (publicKey.size() == KEY_SIZE && privateKey.size() == KEY_SIZE) {
        memcpy(this->publicKey, publicKey.data(), KEY_SIZE);
        memcpy(this->privateKey, privateKey.data(), KEY_SIZE);
        return true;
    }
    return false;
}

/**
 * @brief Get personal public & private keys (used for shared secret generation)
 * 
 * @param publicKey Reference to Bytes to store the public key
 * @param privateKey Reference to Bytes to store the private key
 * 
 * This function copies the current personal public and private keys into the provided Bytes vectors.
 * The vectors will be resized to KEY_SIZE if necessary.
 */
void RadioManager::getPersonalKeys(Bytes& publicKey, Bytes& privateKey) {
    publicKey.resize(KEY_SIZE);
    privateKey.resize(KEY_SIZE);
    memcpy(publicKey.data(), this->publicKey, KEY_SIZE);
    memcpy(privateKey.data(), this->privateKey, KEY_SIZE);
}

/**
 * @brief Set paired channel public & shared keys (used for pairing & E2EE)
 * 
 * @param channel Pairing channel
 * @param publicKey Reference to Bytes containing pub key
 * 
 * @return True if keys set, false if invalid key(s)
 * 
 */
bool RadioManager::setPairedDeviceKeys(uint8_t channel, const Bytes& publicKey) {
    if (channel < MAX_CHANNELS) {
        if (publicKey.size() == KEY_SIZE) {
            uint8_t sharedKey[KEY_SIZE];
            bool keyGen = generateX25519SharedKey(publicKey.data(), privateKey, sharedKey);
            if (!keyGen) return false;
            memcpy(this->pairedDevices[channel].publicKey, publicKey.data(), KEY_SIZE);
            memcpy(this->pairedDevices[channel].sharedKey, sharedKey, KEY_SIZE);
            this->pairedDevices[channel].chaObject.setKey(this->pairedDevices[channel].publicKey);
            return true;
        }
    }
    return false;
}

/**
 * @brief Generate X25519 shared key
 * 
 * @param peerPublicKey Pointer to peer public key (KEY_SIZE)
 * @param privateKey Pointer to own private key (KEY_SIZE)
 * @param sharedKey Pointer to generated shared key (KEY_SIZE)
 * @return true if the key was generated, false otherwise
 */
bool RadioManager::generateX25519SharedKey(const uint8_t* peerPublicKey, const uint8_t* privateKey, uint8_t* sharedKey) {
    mbedtls_ecdh_context ctx;
    mbedtls_ecdh_init(&ctx);

    int ret = mbedtls_ecp_group_load(&ctx.grp, MBEDTLS_ECP_DP_CURVE25519);
    if (ret != 0) {
        mbedtls_ecdh_free(&ctx);
        return false;
    }

    ret = mbedtls_mpi_read_binary(&ctx.d, privateKey, KEY_SIZE);
    if (ret != 0) {
        mbedtls_ecdh_free(&ctx);
        return false;
    }

    ret = mbedtls_ecp_point_read_binary(&ctx.grp, &ctx.Qp, peerPublicKey, KEY_SIZE);
    if (ret != 0) {
        mbedtls_ecdh_free(&ctx);
        return false;
    }

    ret = mbedtls_ecdh_compute_shared(&ctx.grp, &ctx.z, &ctx.Qp, &ctx.d,
                                      mbedtls_ctr_drbg_random, &ctr_drbg);
    if (ret != 0) {
        mbedtls_ecdh_free(&ctx);
        return false;
    }

    ret = mbedtls_mpi_write_binary(&ctx.z, sharedKey, KEY_SIZE);
    if (ret != 0) {
        mbedtls_ecdh_free(&ctx);
        return false;
    }

    mbedtls_ecdh_free(&ctx);
    return true;
}

/**
 * @brief Gets the radio module ID
 * 
 * @return The radio module ID as a String
 */
String RadioManager::getRadioID() {
    return radioID;
}

/**
 * @brief Check validity of a given address
 * 
 * @param addr The address to be checked
 * @return true if the address is valid, false otherwise
 */
bool RadioManager::checkValidAddr(String& addr) {
    // Check if address is encoded on 5 characters
    if (addr.length() != 5) {
        return false;
    }

    // Check if 1st char is a number [0,5]
    char firstChar = addr.charAt(0);
    if (firstChar < '0' || firstChar > '5') {
        return false;
    }

    // Check if last 4 char are alphanumeric
    for (int i = 1; i < 5; i++) {
        if (!isAlphaNumeric(addr.charAt(i))) {
            return false;
        }
    }

    // Valid if fell through all checks
    return true;
}

/**
 * @brief Check if UID exists in paired devices and unpair corresponding address
 * 
 * @param uid The 4-character UID to be unpaired
 * @return true if an address was unpaired, false otherwise
 */
bool RadioManager::clearPairedUID(String& uid) {
    bool ret = false;
    for (int i=0 ; i<MAX_CHANNELS ; i++) {
        String testUID = getPairedUID(i);
        if (testUID == uid) {
            clearPairedAddr(i);
            ret = true;
            break;
        }
    }
    return ret;
}

/**
 * @brief Set the public key for a paired device
 * 
 * @param channel The channel number of the paired device
 * @param newPublicKey Pointer to the new public key
 */
void RadioManager::setDevicePublicKey(uint8_t channel, const uint8_t* newPublicKey) {
    if (channel < MAX_CHANNELS) {
        memcpy(pairedDevices[channel].publicKey, newPublicKey, KEY_SIZE);
    }
}

/**
 * @brief Set the shared key for a paired device and update the chaObject
 * 
 * @param channel The channel number of the paired device
 * @param newSharedKey Pointer to the new shared key
 */
void RadioManager::setDeviceSharedKey(uint8_t channel, const uint8_t* newSharedKey) {
    if (channel < MAX_CHANNELS) {
        memcpy(pairedDevices[channel].sharedKey, newSharedKey, KEY_SIZE);
        pairedDevices[channel].chaObject.setKey(newSharedKey);
    }
}

/**
 * @brief Encrypt a message using the chaObject of the specified channel
 * 
 * @param channel The channel number to use for encryption
 * @param message The message to encrypt
 * @return The encrypted message as a vector of bytes, or an empty vector if encryption fails
 */
Bytes RadioManager::encryptMessage(uint8_t channel, const Bytes& message) {
    if (channel < MAX_CHANNELS) {
        return pairedDevices[channel].chaObject.encrypt(message);
    }
    return Bytes();
}

/**
 * @brief Decrypt a message using the chaObject of the specified channel
 * 
 * @param channel The channel number to use for decryption
 * @param encryptedMessage The encrypted message to decrypt
 * @return The decrypted message as a vector of bytes, or an empty vector if decryption fails
 */
Bytes RadioManager::decryptMessage(uint8_t channel, const Bytes& encryptedMessage) {
    if (channel < MAX_CHANNELS) {
        return pairedDevices[channel].chaObject.decrypt(encryptedMessage);
    }
    return Bytes();
}

/**
 * @brief Add padding to message (fill with 0s)
 * 
 * @param payload Reference to payload Bytes
 * @param paddingSize Padding size in bytes
 */
void RadioManager::pad(Bytes& payload, size_t paddingSize) {
    if (payload.size() < paddingSize) {
        size_t paddingToAdd = paddingSize - payload.size();
        payload.insert(payload.end(), paddingToAdd, 0);
    } else if (payload.size() > paddingSize) {
        payload.resize(paddingSize);
    }
}

/**
 * @brief Remove padding from message (end 0s)
 * 
 * @param payload Reference to payload vector
 */
void RadioManager::unpad(Bytes& payload) {
    while (!payload.empty() && payload.back() == 0) {
        payload.pop_back();
    }
}

/**
 * @brief Clear all messages in inbox
 * 
 * @param channel Channel number
 */
void RadioManager::clearMessages(uint8_t channel) {
    if (channel < MAX_CHANNELS) {
        pairedDevices[channel].mailbox.clear();
    }
}

/**
 * @brief Enable/disable radio operation
 * 
 * @param bool Target state (false=off, true=on)
 */
void RadioManager::enable(bool en) {
    isEnabled = en;
    if (!en) {
        // Clear all mailboxes
        for (int i = 0; i < MAX_CHANNELS; i++) {
            clearMessages(i);
        }
        // Stop listening to radio
        radio.stopListening();
    } else {
        // Resume radio listening
        radio.begin();
    }
}

/**
 * @brief Export the current configuration as a JSON string
 * 
 * @return The configuration as a JSON string
 */
String RadioManager::exportCfg() {
    JsonDocument doc;
    // Export pairedAddr
    doc["pairedDevices"] = getPairedDevicesJson();

    // Export personalKeys
    Bytes pubKey, privKey;
    getPersonalKeys(pubKey, privKey);
    doc["personalKeys"]["publicKey"] = Base64::encode(pubKey.data(), pubKey.size());
    doc["personalKeys"]["privateKey"] = Base64::encode(privKey.data(), privKey.size());

    String output;
    serializeJson(doc, output);
    return output;
}

/**
 * @brief Import the configuration from a JSON string
 * 
 * @param jsonConfig The JSON string to import
 * @return true if the import was successful, false otherwise
 */
bool RadioManager::importCfg(const String& jsonConfig) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, jsonConfig);

    if (error) {
        return false;
    }

    // Import personalKeys
    if (doc["personalKeys"].is<JsonObject>()) {
        String pubKeyStr = doc["personalKeys"]["publicKey"];
        String privKeyStr = doc["personalKeys"]["privateKey"];
        Bytes pubKey, privKey;
        Base64::decode(pubKeyStr, pubKey);
        Base64::decode(privKeyStr, privKey);
        setPersonalKeys(pubKey, privKey);
    }

    // Import pairedAddr & keys
    if (doc["pairedDevices"].is<JsonObject>()) {
        String pairedAddrJson = doc["pairedDevices"].as<String>();
        setPairedDevicesJson(pairedAddrJson);
    }

    return true;
}