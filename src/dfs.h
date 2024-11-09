#ifndef DFS_H
#define DFS_H

#include <Arduino.h>
#include <SPIFFS.h>
#include "RadioManager.h"

#define BUTTON_PIN D1

extern RadioManager radioManager;

// Button management variables
bool lastButtonState = HIGH;
unsigned long buttonPressStartTime = 0;
const unsigned long PAIRING_BUTTON_DURATION = 1000; // 1 second

// Radio variables
String lastSavedPairedAddrList = "";

// Message sending variables
uint8_t currentChannel = 0;
uint8_t messageStatus = 0;
bool messageSending = false;

// LED blinking control variables
unsigned long lastLedUpdate = 0;
unsigned long ledPeriod = 1000;
int ledBlinkCount = 0;
int currentBlinkCount = 0;
bool isLedOn = false;
int ledWait = 0;
unsigned long lastBlinkSeries = 0;

// Configuration file name in SPIFFS
#define CONFIG_FILE "/radio_config.json"

// Function to get a 4-digit UID based on MAC address
const char* getESP32UID() {
    static char uid[5];
    uint8_t baseMac[6];
    esp_read_mac(baseMac, ESP_MAC_WIFI_STA);
    snprintf(uid, sizeof(uid), "%02X%02X", baseMac[4], baseMac[5]);
    return uid;
}

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

// Function to save the configuration
bool saveCfg() {
    String cfg = radioManager.exportCfg();
    File file = SPIFFS.open(CONFIG_FILE, FILE_WRITE);
    if (!file) {
        Serial.println("Failed to open file for writing");
        return false;
    }
    size_t bytesWritten = file.print(cfg);
    file.close();
    if (bytesWritten == cfg.length()) {
        Serial.println("Configuration saved successfully");
        lastSavedPairedAddrList = radioManager.getPairedDevicesJson();
        return true;
    } else {
        Serial.println("Failed to save configuration");
        return false;
    }
}

// Function to restore the configuration
bool retrieveCfg() {
    if (SPIFFS.exists(CONFIG_FILE)) {
        File file = SPIFFS.open(CONFIG_FILE, FILE_READ);
        if (!file) {
            Serial.println("Failed to open file for reading");
            return false;
        }
        String cfg = file.readString();
        file.close();
        if (cfg.length() > 0) {
            if (radioManager.importCfg(cfg)) {
                Serial.println("Configuration restored successfully");
                lastSavedPairedAddrList = radioManager.getPairedDevicesJson();
                return true;
            } else {
                Serial.println("Error while restoring configuration");
                return false;
            }
        }
    } else {
        Serial.println("No saved configuration found");
        return false;
    }
    return false;
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

#endif // DFS_H
