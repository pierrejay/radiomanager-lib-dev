#include <Arduino.h>
#include <SPI.h>
#include "RadioManager.h"
#include <esp_system.h>

// Function to get a 4-digit UID based on MAC address
const char* getESP32UID() {
    static char uid[5];
    uint8_t baseMac[6];
    esp_read_mac(baseMac, ESP_MAC_WIFI_STA);
    snprintf(uid, sizeof(uid), "%02X%02X", baseMac[4], baseMac[5]);
    return uid;
}

// Unique identifier for this device
const char* RADIO_ID = getESP32UID();

// Pin definitions
#define CE_PIN D2
#define CSN_PIN D7
#define BUTTON_PIN D1

// Create RadioManager instance
RadioManager radioManager(CE_PIN, CSN_PIN, RADIO_ID);

// Button management variables
bool lastButtonState = HIGH;
unsigned long buttonPressStartTime = 0;
const unsigned long PAIRING_BUTTON_DURATION = 1000; // 1 second

// Message sending variables
uint8_t currentChannel = 0;
int messageStatus = 0;
bool messageSending = false;

// LED blinking control variables
unsigned long lastLedUpdate = 0;
unsigned long ledPeriod = 1000;
int ledBlinkCount = 0;
int currentBlinkCount = 0;
bool isLedOn = false;
int ledWait = 0;
unsigned long lastBlinkSeries = 0;

void setLedMode(int blink, unsigned long period, int wait) {
  ledBlinkCount = blink;
  ledPeriod = period;
  ledWait = wait;
  currentBlinkCount = 0;
  isLedOn = false;
  lastLedUpdate = millis();
  lastBlinkSeries = millis();
}

void updateLed() {
  unsigned long currentTime = millis();
  
  if (ledBlinkCount == 0 || currentTime - lastBlinkSeries >= ledWait) {
    if (currentTime - lastLedUpdate >= ledPeriod / 2) {
      lastLedUpdate = currentTime;
      
      if (ledBlinkCount == 0 || currentBlinkCount < ledBlinkCount * 2) {
        isLedOn = !isLedOn;
        digitalWrite(LED_BUILTIN, isLedOn ? LOW : HIGH);
        
        if (ledBlinkCount > 0) {
          currentBlinkCount++;
          if (currentBlinkCount >= ledBlinkCount * 2) {
            if (ledWait >= 0) {
              lastBlinkSeries = currentTime;
              currentBlinkCount = 0;
            } else {
              ledBlinkCount = 0; // Stop blinking if wait is -1
            }
          }
        }
      } else {
        digitalWrite(LED_BUILTIN, HIGH);
      }
    }
  }
}

void setup() {
    Serial.begin(115200);
    delay(5000);
    Serial.println("Starting RadioManager example");

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
}

void handleButton() {
    bool currentButtonState = digitalRead(BUTTON_PIN);
    
    if (currentButtonState == LOW && lastButtonState == HIGH) {
        // Button just pressed
        buttonPressStartTime = millis();
        Serial.println("Button pressed!");
    } else if (currentButtonState == HIGH && lastButtonState == LOW) {
        // Button just released
        if (millis() - buttonPressStartTime >= PAIRING_BUTTON_DURATION) {
            Serial.println("Starting pairing procedure...");
            radioManager.startPairing();
        }
    }
    
    lastButtonState = currentButtonState;
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
                    Serial.println("Trying to send message '" + message + "' on channel " + String(channel));
                    if (radioManager.sendMsg(message, channel, &messageStatus, true)) {
                        messageSending = true;
                        Serial.println("Success");
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

    // Handle radio operations
    radioManager.loop();

    // Check current state
    if (radioManager.isAvailable()) {
        // Handle button for pairing
        handleButton();

        // Check message sending status
        checkSendingStatus();

        // Read messages
        readMessages();

        // Send serial messages
        sendSerialMessage();
    }

    // LED blinking code (commented out for now)
    // updateLed();
    // ... (rest of the LED blinking code)
}