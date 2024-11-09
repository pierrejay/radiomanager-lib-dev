#include <Arduino.h>
#include <SPI.h>
#include "RadioManager.h"
#include <esp_system.h>
#include <SPIFFS.h>
#include "dfs.h"

// Configuration file name in SPIFFS
#define CONFIG_FILE "/radio_config.json"

// Unique identifier for this device
const char* RADIO_ID = getESP32UID();

// Pin definitions
#define CE_PIN D2
#define CSN_PIN D7

// Create RadioManager instance
RadioManager radioManager(CE_PIN, CSN_PIN, RADIO_ID);

void setup() {
    Serial.begin(9600);
    delay(5000);
    Serial.println("Starting RadioManager example");

    // Initialize SPIFFS
    if (!SPIFFS.begin(true)) {
        Serial.println("Failed to initialize SPIFFS");
        while (1);
    }

    // Button configuration
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    pinMode(LED_BUILTIN, OUTPUT);

    // RadioManager initialization
    if (!radioManager.begin()) {
        Serial.println("Failed to initialize RadioManager");
        while (1) { delay(1000); } // Infinite loop in case of failure
    }

    Serial.println("RadioManager initialized successfully");
    Serial.println("Press the button for 1 second to start pairing");

    // Display the radio module ID
    Serial.print("Radio ID: ");
    Serial.println(radioManager.getRadioID());

    // Restore configuration after reboot
    if (!retrieveCfg()) {
        Serial.println("Failed to retrieve configuration, using default settings");
    }
}

void loop() {
    // Handle radio operations
    radioManager.loop();

    // Check current state
    if (radioManager.isAvailable()) {

        // Process radio data
        handleButton();
        checkSendingStatus();
        readMessages();
        sendSerialMessage();

        // Check if pairing has been done and save the configuration
        String currentPairedAddrJson = radioManager.getPairedDevicesJson();
        if (currentPairedAddrJson != lastSavedPairedAddrList) {
            if (!saveCfg()) {
                Serial.println("Failed to save configuration");
                lastSavedPairedAddrList = currentPairedAddrJson;
            }
        }
    }

    updateLed();
}
