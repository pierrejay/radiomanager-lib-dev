# RadioManager Library Documentation

## Overview

RadioManager is a simple Arduino library for wireless communication using NRF24 modules. The idea was to get a lightweight & async solution for point-to-point communication in different embedded applications with dynamic pairing management at runtime (no need to hardcode addressing, same code for all transceivers) & basic encryption features.

This library was built for the ESP-32 and uses its internal TRNG. This should be modified to work properly on other platforms.

### Highlights
- **Multi-Device Pairing:** Pair with up to 5 modules dynamically. Automatically assigns unique addresses to each paired module.
- **Bidirectional communication:** Operates as both a transmitter and receiver.
- **Secure Communication:** End-to-end encryption support with minimal overhead: X25519 key exchange + ChaCha20 E2EE
- **Asynchronous operations:** Non-blocking sending and receiving with progress monitoring.
- **Large Message Support:** Automatic fragmentation and reassembly for messages over 32 bytes with integrity check.
- **Mailbox System:** Buffered communication with a FIFO system to manage high traffic.
- **Export/Import configuration:** Easily export and import pairing data for persistence across sessions.

## Library description

### Utility
The Utility group manages the core functionalities essential to the operation of the library. The `begin()` function initializes the NRF24 radio, setting it up for communication, while the `loop()` function handles the main execution loop, ensuring radio operations such as sending or receiving messages are executed efficiently. These operations are non-blocking, meaning the main application can continue running while messages are processed in the background. Functions like `getCurrentState()`, `isBusy()`, and `isAvailable()` provide real-time monitoring of the device’s state, helping to determine if it is currently busy with a task or ready to process new actions. Additionally, `getRadioID()` retrieves the unique identifier of the radio node, and `startPairing()` manages the initiation of the pairing process.

### Radio
The Radio group focuses on the low-level management of the NRF24 module, defining its configuration and operational parameters. This includes setting up communication configuration and data channels for message transmission. Parameters like `RECEIVE_TIMEOUT`, `PAIRING_TIMEOUT` and `PAIRING_INTERVAL` control the timing of radio transmissions. These parameters ensure the stability and reliability of radio communication, handling issues like transmission delays or missed responses efficiently.

### Encryption
Messages are securely exchanged between nodes and cannot be replayed thanks to an automatic counter index implemented in the custom `SimpleCha2` class. 

The library handles secure generation of an ED25519 key pair at initialization for each node (with `mbedtls` methods and the ESP-32 TRNG), as well as ED25519 key exchange : during the pairing process, the public keys are exchanged between nodes, allowing them to securely generate a shared secret for encrypted communication.

NB: keys are stored in memory so anybody having access to the hardware could compromise them, plus the raw shared secret is used as a key, so it doesn't provide a high level of safety, just enough to avoid intruders eavesdropping on the radio exchanges or impersonating nodes on the network.

### Addressing
Addressing is handled dynamically : each node is given a 4-character alphanumeric UID (e.g. X2d8), allowing up to 11 M IDs. The library supports dynamic pairing with up to five other nodes, each assigned a dedicated communication channel materialized by a 5-character address.

An address is formed of a pipe number from 1 to 5 + the module UID (in our examples, 1X2d8 to 5X2d8). Each module distributes one of its 5 available addresses for other nodes to communicate with. In the library, these paired devices are arranged in 5 `channel`s from 0 to 4.

### Pairing
Pairing between nodes is managed automatically by the RadioManager, streamlining the procedure of establishing secure communication links. When pairing is initiated via `startPairing()` on two different nodes, they exchange public keys, generate shared encryption keys, and assign communication addresses while validating encryption. This process is managed over a specified pairing channel, with timeouts and retries built-in to ensure reliability. 

Once paired, nodes can maintain their connections or be easily unpaired if needed by starting the pairing process again. The procedure is automatic and fully asynchronous, and can be linked e.g. to pressing a button. It is also possible to use methods like `getPairedAddr()` and `setPairedAddr()` to manage the paired addresses list manually.

### Message Handling & Structure
Message handling in the library supports both short and long messages, using fragmentation and reassembly to manage message sizes beyond the NRF24's 32-byte packet limit. Messages can be up to 2048 bytes in length by default, and are automatically split into smaller packets (after being encrypted, if so). Transmission of fragments is tracked through a header to ensure the integrity of long messages as they are transmitted across multiple packets. 

Messages can be sent to a paired node in String or raw bytes format (`Bytes` = `std::vector<uint8_t>`). We noticed using dynamic payloads were not properly working on a few NRF24L01+ modules, so the library uses padding to stuff all radio packets up to 32 bytes and clean them at reception.

The library also features a mailbox system, where each paired device has a buffer for storing several messages (up to 3 by default). This prevents data loss during periods of high traffic and ensures that all messages are processed in the order they were received, following a First-In-First-Out (FIFO) system. 

The "message API" provides simple methods to send & read messages. All radio transmissions are done asynchronously thanks to the `loop()` method of the library, which needs to be called regularly in the main application thread so that outbound messages are processed and inbound messages are fetched to the mailbox. Only one message can be sent at a time but its status can be checked throughout the process by providing a pointer to a tracker variable.

## Usage

### Basic Setup
To use the RadioManager library, you need to define a unique radio ID for your node. This ID is a 4-character string that identifies the node in the network. After setting up the ID, create an instance of `RadioManager`, then initialize the radio with `radioManager.begin()`, in `setup()` and call `radioManager.loop()` in the main `loop()` function to handle ongoing radio operations. 

For example:
```cpp
RadioManager radioManager(CE_PIN, CSN_PIN, "NODE");  // Define your radio ID
radioManager.begin();  // Initialize the radio
```

The `radioManager.loop()` function should be called continuously within the main `loop()` function to handle all radio operations such as message transmission and reception. It ensures the system is responsive and handles non-blocking communication.

### Pairing Management
Pairing is a fundamental part of establishing communication between nodes. Use `radioManager.startPairing()` to initiate the pairing process. During pairing, the nodes exchange public keys and establish secure communication links. You can trigger the pairing process based on a button press or other event in your application.

Example for starting the pairing on a button press:
```cpp
if (digitalRead(BUTTON_PIN) == LOW) {
    radioManager.startPairing();
}
```

### Sending and Receiving Messages
Once nodes are paired, you can send messages to a specific channel or automatically search for a known UID and send the message. Use the `radioManager.sendMsg()` function to send a message and `radioManager.readMsg()` to retrieve received messages. 

The sending status can be tracked using a status variable, which indicates whether the message was successfully sent or not. Messages can be sent encrypted by setting the encryption flag.

Example of sending a message:
```cpp
String message = "Hello World";
int messageStatus;
radioManager.sendMsg(message, 0, &messageStatus, true);  // Send encrypted message to channel 0
```

To read incoming messages, use:
```cpp
if (radioManager.isMsgAvailable(0)) {
    Bytes msg = radioManager.readMsg(0);  // Read message from channel 0
    Serial.println(String((char*)msg.data(), msg.size()));
}
```

### Example `main.cpp`
Here is an example C++ code demonstrating the basic usage of the RadioManager library. The ESP32 node pairs with other nodes on a button press, sends any serial input over the network, and retransmits messages received on its paired channels.

```cpp
#include "RadioManager.h"

#define CE_PIN 5
#define CSN_PIN 4
#define BUTTON_PIN 0

RadioManager radioManager(CE_PIN, CSN_PIN, "NODE");
bool messageSending = false;
int messageStatus = 0;

void setup() {
    Serial.begin(115200);
    radioManager.begin();
    pinMode(BUTTON_PIN, INPUT_PULLUP);
}

void handleButton() {
    if (digitalRead(BUTTON_PIN) == LOW) {
        radioManager.startPairing();
    }
}

void sendSerialMessage() {
    if (Serial.available()) {
        String message = Serial.readStringUntil('\n');
        message.trim();
        
        if (!message.isEmpty()) {
            for (uint8_t channel = 0; channel < RadioManager::MAX_CHANNELS; channel++) {
                String pairedUID = radioManager.getPairedAddr(channel);
                if (!pairedUID.isEmpty()) {
                    messageStatus = 0;
                    if (radioManager.sendMsg(message, channel, &messageStatus, true)) {
                        messageSending = true;
                    }
                }
            }
        }
    }
}

void checkSendingStatus() {
    if (messageSending) {
        if (messageStatus == 1) {
            Serial.println("Message sent successfully");
            messageSending = false;
        } else if (messageStatus == -1) {
            Serial.println("Failed to send message");
            messageSending = false;
        }
    }
}

void readMessages() {
    for (uint8_t channel = 0; channel < RadioManager::MAX_CHANNELS; channel++) {
        while (radioManager.isMsgAvailable(channel)) {
            Bytes readMsg = radioManager.readMsg(channel);
            String receivedMsg = String((char*)readMsg.data(), readMsg.size());
            Serial.println("Message received on channel " + String(channel) + ": " + receivedMsg);
        }
    }
}

void loop() {
    radioManager.loop();
    
    if (radioManager.isAvailable()) {
        handleButton();
        checkSendingStatus();
        readMessages();
        sendSerialMessage();
    }
}
```

## Full API Reference

### Constructor

```cpp
RadioManager(uint8_t ce_pin, uint8_t csn_pin, const char* radio_id)
```

Initializes a new RadioManager instance.
- `ce_pin`: CE pin for the nRF24L01 module
- `csn_pin`: CSN pin for the nRF24L01 module
- `radio_id`: Unique identifier for this radio (will be trimmed to 4 characters)

### Core Methods

#### begin()
```cpp
bool begin()
```
Initializes the radio module and configures initial parameters.
- **Returns**: `true` if initialization was successful, `false` otherwise

#### loop()
```cpp
void loop()
```
Main function to be called frequently in the program's main loop. Manages the different states of the RadioManager.

#### sendMsg()
```cpp
bool sendMsg(const Bytes& msg, uint8_t channel, uint8_t* status = nullptr, bool encryption = false)
```
Sends a message on a specific channel.
- `msg`: The message to send
- `channel`: The channel number (0-4)
- `status`: Optional pointer to track sending progress (0=in progress, -1=error, 1=success)
- `encryption`: Whether to encrypt the message
- **Returns**: `true` if sending was initiated successfully, `false` otherwise

#### readMsg()
```cpp
Bytes readMsg(uint8_t channel)
```
Reads an available message from a specific channel.
- `channel`: The channel number to read from (0-4)
- **Returns**: The message as a vector of bytes, or empty vector if no message available

### Pairing Management

#### startPairing()
```cpp
bool startPairing()
```
Initiates the secure pairing process with another device.
- **Returns**: `true` if pairing process started successfully, `false` otherwise

#### getPairedAddr()
```cpp
String getPairedAddr(uint8_t channel)
```
Gets the address of a paired device on a specific channel.
- `channel`: The channel number (0-4)
- **Returns**: The address of the paired device, or empty string if no device is paired

## States

The RadioManager can be in one of the following states:

| State | Description |
|-------|-------------|
|`IDLE`| Ready to send/receive messages|
|`TRANSMITTING`| Currently sending a message |
|`RECEIVING`| Currently receiving a message |
|`PAIRING_LISTEN`| Listening for pairing requests |
|`PAIRING_TRANSMIT`| Transmitting pairing information |

### State Query Methods

```cpp
State getCurrentState()  // Returns current state
bool isBusy()           // Returns true if radio is busy
bool isAvailable()      // Returns true if radio is available
```

## Configuration Management

### exportCfg()
```cpp
String exportCfg()
```
Exports the current configuration as a JSON string, including paired devices and encryption keys.
- **Returns**: Configuration as JSON string

### importCfg()
```cpp
bool importCfg(const String& jsonConfig)
```
Imports configuration from a JSON string.
- `jsonConfig`: JSON string containing configuration
- **Returns**: `true` if import was successful, `false` otherwise

### Debugging
You can enable detailed logs for troubleshooting by setting the flag `RADIO_MANAGER_DEBUG` in the `.cpp` file. This will activate verbose output, helping you to monitor the internal operations of the library during development.

## License
MIT License