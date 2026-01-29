/**
 * @file ESP32_Sender_GCM.ino
 * @brief Secure ESP-NOW Remote Control Sender using AES-128-GCM.
 * @author iamfurkann
 * @date 2026-01-29
 * * Update Notes:
 * - Added packet decryption to read ACK response.
 * - Synced session counter with Receiver to prevent Replay Attack errors.
 */

#include <esp_now.h>
#include <WiFi.h>
#include "mbedtls/gcm.h"

// =============================================================
// CONFIGURATION
// =============================================================

// Receiver (ESP8266) MAC Address
const uint8_t RECEIVER_MAC[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// AES-128 Key (Must match Receiver)
const uint8_t AES_KEY[16] = { 
    0x2B, 0x7E, 0x15, 0x16, 0x28, 0xAE, 0xD2, 0xA6, 
    0xAB, 0xF7, 0x15, 0x88, 0x09, 0xCF, 0x4F, 0x3C 
};

// GCM Parameters
#define IV_SIZE 12
#define TAG_SIZE 16

// =============================================================
// DATA STRUCTURES
// =============================================================

enum MessageType {
    MSG_SYN  = 1,
    MSG_ACK  = 2,
    MSG_DATA = 3,
    MSG_RST  = 4
};

// Payload (Plaintext)
typedef struct __attribute__((packed)) {
    uint8_t msgType;     
    uint32_t packetId;   
    int16_t joystickX;  
    int16_t joystickY;  
    uint8_t buttons;
} Payload;

// Encrypted Packet
typedef struct __attribute__((packed)) {
    uint8_t iv[IV_SIZE];
    uint8_t tag[TAG_SIZE];
    uint8_t ciphertext[sizeof(Payload)];
} SecurePacket;

// =============================================================
// GLOBALS
// =============================================================

esp_now_peer_info_t peerInfo;
Payload currentPayload;
Payload incomingPayload; // To store decrypted ACK data
uint32_t sessionCounter = 0;
bool isConnected = false;

// =============================================================
// CRYPTO HELPERS
// =============================================================

/**
 * @brief Encrypts payload using AES-128-GCM
 */
bool encryptAndSend(Payload* payload) {
    SecurePacket packet;
    mbedtls_gcm_context aes;
    
    mbedtls_gcm_init(&aes);
    mbedtls_gcm_setkey(&aes, MBEDTLS_CIPHER_ID_AES, AES_KEY, 128);

    // IV Generation: Use packetId
    memset(packet.iv, 0, IV_SIZE);
    memcpy(packet.iv, &payload->packetId, sizeof(uint32_t)); 

    // GCM Encryption
    int ret = mbedtls_gcm_crypt_and_tag(
        &aes, 
        MBEDTLS_GCM_ENCRYPT, 
        sizeof(Payload), 
        packet.iv, IV_SIZE, 
        NULL, 0, 
        (const unsigned char*)payload, 
        packet.ciphertext, 
        TAG_SIZE,  // Corrected parameter position
        packet.tag
    );

    mbedtls_gcm_free(&aes);

    if (ret != 0) {
        Serial.println("[Error] Encryption Failed");
        return false;
    }

    esp_err_t result = esp_now_send(RECEIVER_MAC, (uint8_t*)&packet, sizeof(SecurePacket));
    return (result == ESP_OK);
}

/**
 * @brief Decrypts and Verifies incoming GCM packet (for ACK)
 */
bool decryptAndVerify(const uint8_t* rawData, Payload* outPayload) {
    SecurePacket* packet = (SecurePacket*)rawData;
    mbedtls_gcm_context aes;
    
    mbedtls_gcm_init(&aes);
    mbedtls_gcm_setkey(&aes, MBEDTLS_CIPHER_ID_AES, AES_KEY, 128);

    // GCM Auth Decrypt
    // mbedtls_gcm_auth_decrypt performs both decryption and tag verification
    int ret = mbedtls_gcm_auth_decrypt(
        &aes,
        sizeof(Payload),
        packet->iv, IV_SIZE,
        NULL, 0,
        packet->tag, TAG_SIZE,
        packet->ciphertext,
        (unsigned char*)outPayload
    );

    mbedtls_gcm_free(&aes);

    if (ret != 0) {
        // ret == MBEDTLS_ERR_GCM_AUTH_FAILED if tag mismatch
        return false; 
    }
    return true; 
}

// =============================================================
// CALLBACKS
// =============================================================

void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    // Debug info if needed
}

void onDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
    if (len != sizeof(SecurePacket)) return;

    // 1. Decrypt the incoming packet (Expecting ACK)
    if (!decryptAndVerify(incomingData, &incomingPayload)) {
        Serial.println("[Security] Invalid ACK Received (Auth Failed)");
        return;
    }

    if (incomingPayload.msgType == MSG_RST) {
        Serial.println("[Info] Receiver requested RESET. Restarting Handshake...");
        isConnected = false;      // Baglantiyi kopar
        sessionCounter = 0;       // Sayaci sifirla
        return;
    }

    // 2. Process Handshake ACK
    if (incomingPayload.msgType == MSG_ACK) {
        // CRITICAL FIX: Sync our counter with Receiver's random counter
        sessionCounter = incomingPayload.packetId;
        
        if (!isConnected) {
            isConnected = true;
            Serial.print("[Info] Connected! Synced Session ID: ");
            Serial.println(sessionCounter);
        }
    }
}

// =============================================================
// MAIN LOOPS
// =============================================================

void setup() {
    Serial.begin(115200);
    WiFi.mode(WIFI_STA);

    if (esp_now_init() != ESP_OK) {
        Serial.println("[Error] ESP-NOW Init Failed");
        return;
    }

    esp_now_register_send_cb(onDataSent);
    esp_now_register_recv_cb(onDataRecv);

    memcpy(peerInfo.peer_addr, RECEIVER_MAC, 6);
    peerInfo.channel = 0;  
    peerInfo.encrypt = false;
    
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("[Error] Failed to add peer");
        return;
    }

    Serial.println("[Info] Starting Handshake...");
}

void loop() {
    // 1. Handshake Phase
    if (!isConnected) {
        currentPayload.msgType = MSG_SYN;
        currentPayload.packetId = 0; 
        
        encryptAndSend(&currentPayload);
        Serial.println("Sending SYN...");
        delay(1000); // Retry every 1s
        return;
    }

    // 2. Data Transmission Phase
    // Increment counter based on the SYNCED value from ACK
    sessionCounter++;
    
    currentPayload.msgType = MSG_DATA;
    currentPayload.packetId = sessionCounter;
    currentPayload.joystickX = random(0, 4095); // Example Data
    currentPayload.joystickY = random(0, 4095);
    currentPayload.buttons = 0x01;

    encryptAndSend(&currentPayload);
    
    // Serial.printf("Sent Packet ID: %u\n", sessionCounter); // Debug
    delay(20); 
}
