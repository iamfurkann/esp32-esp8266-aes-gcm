/**
 * @file ESP8266_Receiver_GCM.ino
 * @brief Secure ESP-NOW Receiver using AES-128-GCM (BearSSL).
 * @author iamfurkann
 * @date 2026-01-29
 * * Features:
 * - Decrypts and Authenticates packets using BearSSL GCM.
 * - Protection against Replay Attacks (Counter check).
 * - Secure Handshake response.
 */

#include <ESP8266WiFi.h>
#include <espnow.h>
#include <bearssl/bearssl.h>

// =============================================================
// CONFIGURATION
// =============================================================

// Sender (ESP32) MAC Address - REPLACE THIS
uint8_t SENDER_MAC[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

const uint8_t AES_KEY[16] = { 
    0x2B, 0x7E, 0x15, 0x16, 0x28, 0xAE, 0xD2, 0xA6, 
    0xAB, 0xF7, 0x15, 0x88, 0x09, 0xCF, 0x4F, 0x3C 
};

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

typedef struct __attribute__((packed)) {
    uint8_t msgType;     
    uint32_t packetId;   
    int16_t joystickX;  
    int16_t joystickY;  
    uint8_t buttons;
} Payload;

typedef struct __attribute__((packed)) {
    uint8_t iv[IV_SIZE];
    uint8_t tag[TAG_SIZE];
    uint8_t ciphertext[sizeof(Payload)];
} SecurePacket;

// =============================================================
// GLOBALS
// =============================================================

SecurePacket rxPacket; // Buffer for incoming
Payload decodedPayload;
uint32_t lastSessionCounter = 0;
bool handshakeCompleted = false;

// =============================================================
// CRYPTO HELPERS (BearSSL)
// =============================================================

/**
 * @brief Decrypts and Verifies GCM packet
 * @return true if Tag is valid (Authentic), false otherwise
 */
bool decryptAndVerify(const uint8_t* incomingData, Payload* outPayload) {
    // 1. Map incoming data to SecurePacket struct
    const SecurePacket* packet = (const SecurePacket*)incomingData;
    
    // 2. Setup BearSSL GCM Context
    br_aes_ct_ctr_keys ctrCtx;
    br_aes_ct_ctr_init(&ctrCtx, AES_KEY, 16);
    
    br_gcm_context gcmCtx;
    br_gcm_init(&gcmCtx, &ctrCtx.vtable, br_ghash_ctmul32);

    // 3. Reset with IV
    br_gcm_reset(&gcmCtx, packet->iv, IV_SIZE);

    // 4. Decrypt (BearSSL decrypts in place or to buffer)
    // We decrypt directly into the output payload struct
    // Important: In GCM, we process the ciphertext to get plaintext
    uint8_t tempBuffer[sizeof(Payload)];
    memcpy(tempBuffer, packet->ciphertext, sizeof(Payload));
    
    br_gcm_flip(&gcmCtx); // Switch to decryption mode
    br_gcm_run(&gcmCtx, 0, tempBuffer, sizeof(Payload));
    
    // 5. Verify Tag
    uint8_t computedTag[TAG_SIZE];
    br_gcm_get_tag(&gcmCtx, computedTag);

    // Constant-time comparison to prevent timing attacks
    uint16_t diff = 0;
    for (int i = 0; i < TAG_SIZE; i++) {
        diff |= (computedTag[i] ^ packet->tag[i]);
    }

    if (diff != 0) {
        return false; // Auth Failed! Data is corrupted or faked.
    }

    // Auth OK, copy data
    memcpy(outPayload, tempBuffer, sizeof(Payload));
    return true;
}

/**
 * @brief Encrypts a payload (used for sending ACK)
 */
void sendSecureAck(uint32_t counter) {
    SecurePacket packet;
    Payload ackPayload;
    
    ackPayload.msgType = MSG_ACK;
    ackPayload.packetId = counter;
    ackPayload.joystickX = 0;
    ackPayload.joystickY = 0;
    ackPayload.buttons = 0;

    // Setup BearSSL
    br_aes_ct_ctr_keys ctrCtx;
    br_aes_ct_ctr_init(&ctrCtx, AES_KEY, 16);
    br_gcm_context gcmCtx;
    br_gcm_init(&gcmCtx, &ctrCtx.vtable, br_ghash_ctmul32);

    // IV Generation (Use counter)
    memset(packet.iv, 0, IV_SIZE);
    memcpy(packet.iv, &counter, sizeof(uint32_t)); 

    br_gcm_reset(&gcmCtx, packet.iv, IV_SIZE);
    
    // Encrypt
    memcpy(packet.ciphertext, &ackPayload, sizeof(Payload));
    br_gcm_run(&gcmCtx, 1, packet.ciphertext, sizeof(Payload)); // 1 = Encrypt
    
    // Get Tag
    br_gcm_get_tag(&gcmCtx, packet.tag);

    esp_now_send(SENDER_MAC, (uint8_t*)&packet, sizeof(SecurePacket));
}

/**
 * @brief Sends a Reset Request to force sender to re-handshake
 */
void sendSecureReset() {
    SecurePacket packet;
    Payload rstPayload;
    
    rstPayload.msgType = MSG_RST; // Reset Tipi
    rstPayload.packetId = 0;      // Onemsiz
    rstPayload.joystickX = 0;
    rstPayload.joystickY = 0;
    rstPayload.buttons = 0;

    // BearSSL Setup
    br_aes_ct_ctr_keys ctrCtx;
    br_aes_ct_ctr_init(&ctrCtx, AES_KEY, 16);
    br_gcm_context gcmCtx;
    br_gcm_init(&gcmCtx, &ctrCtx.vtable, br_ghash_ctmul32);

    // IV Generation (Random IV for Reset to avoid replay issues)
    uint32_t randomIV = random(0, 0xFFFFFFFF);
    memset(packet.iv, 0, IV_SIZE);
    memcpy(packet.iv, &randomIV, sizeof(uint32_t)); 

    br_gcm_reset(&gcmCtx, packet.iv, IV_SIZE);
    
    // Encrypt
    memcpy(packet.ciphertext, &rstPayload, sizeof(Payload));
    br_gcm_run(&gcmCtx, 1, packet.ciphertext, sizeof(Payload));
    
    // Get Tag
    br_gcm_get_tag(&gcmCtx, packet.tag);

    esp_now_send(SENDER_MAC, (uint8_t*)&packet, sizeof(SecurePacket));
    Serial.println("[Info] Desync detected. MSG_RST sent.");
}

// =============================================================
// CALLBACKS
// =============================================================

void onDataRecv(uint8_t *mac, uint8_t *incomingData, uint8_t len) {
    if (len != sizeof(SecurePacket)) {
        // Invalid size, drop immediately
        return;
    }

    // Attempt to Decrypt and Verify
    if (!decryptAndVerify(incomingData, &decodedPayload)) {
        Serial.println("[Security] Auth Failed! Invalid Tag.");
        return;
    }

    // --- Message Handling ---

    switch (decodedPayload.msgType) {
        case MSG_SYN:
            Serial.println("[Info] SYN Received. Handshake Start.");
            // Reset or Randomize Session
            lastSessionCounter = random(1000, 50000); 
            handshakeCompleted = true;
            
            // Send Encrypted ACK
            sendSecureAck(lastSessionCounter);
            Serial.printf("[Info] ACK Sent. Session ID: %u\n", lastSessionCounter);
            break;

        case MSG_DATA:
            if (!handshakeCompleted) {
                sendSecureReset();
                return;
            }

            // Replay Protection: Check if counter is new
            if (decodedPayload.packetId > lastSessionCounter) {
                lastSessionCounter = decodedPayload.packetId;
                
                // Valid Data Process
                Serial.printf("ID: %u | X: %d | Y: %d\n", 
                    decodedPayload.packetId, 
                    decodedPayload.joystickX, 
                    decodedPayload.joystickY);
                    
                // Implementation: driveMotors(decodedPayload.joystickX...);
            } else {
                Serial.println("[Security] Replay Attack Detected (Old Counter)");
            }
            break;
            
        default:
            break;
    }
}

void onDataSent(uint8_t *mac_addr, uint8_t sendStatus) {
    // Used for ACK send status
}

// =============================================================
// MAIN LOOPS
// =============================================================

void setup() {
    Serial.begin(115200);
    WiFi.mode(WIFI_STA);

    if (esp_now_init() != 0) {
        Serial.println("Error initializing ESP-NOW");
        return;
    }

    esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
    esp_now_register_recv_cb(onDataRecv);
    esp_now_register_send_cb(onDataSent);

    esp_now_add_peer(SENDER_MAC, ESP_NOW_ROLE_COMBO, 1, NULL, 0);

    Serial.println("[Info] Receiver Ready. Waiting for Secure Packets...");
}

void loop() {
}
