/**
 * @file HardwareIdentity.ino
 * @brief Universal MAC Address & Board Info Dumper for ESP32/ESP8266.
 * @author iamfurkann
 * @date 2026-01-29
 * @version 1.1.0
 * * * Description:
 * This utility prints the unique MAC address of the device in two formats:
 * 1. Standard String (AA:BB:CC:DD:EE:FF)
 * 2. C-Style Array   ({0xAA, 0xBB, ...}) -> Ready to copy-paste into code!
 */

#include <Arduino.h>

// Platform-specific Wi-Fi includes
#if defined(ESP32)
  #include <WiFi.h>
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
#else
  #error "Unsupported platform! Please use ESP32 or ESP8266."
#endif

// =============================================================
// CONFIGURATION
// =============================================================

#define SERIAL_BAUD_RATE 115200

// =============================================================
// HELPER FUNCTIONS
// =============================================================

/**
 * @brief Returns the board model string based on preprocessor directives.
 */
const char* getBoardModel() {
  #if defined(ESP32)
    return "ESP32 (WROOM/WROVER/S3/C3)";
  #elif defined(ESP8266)
    return "ESP8266 (ESP-12F/NodeMCU/Wemos)";
  #else
    return "Unknown Generic Board";
  #endif
}

/**
 * @brief Prints the MAC address formatted as a C byte array.
 * Example Output: {0xA8, 0x42, 0xE3, 0x83, 0x76, 0x7C}
 */
void printMacAsCArray() {
  uint8_t mac[6];
  WiFi.macAddress(mac);

  Serial.print("C-Array Format: { ");
  
  for (int i = 0; i < 6; i++) {
    Serial.print("0x");
    if (mac[i] < 0x10) Serial.print("0"); // Add leading zero if needed
    Serial.print(mac[i], HEX);
    
    if (i < 5) Serial.print(", "); // Add comma between bytes
  }
  
  Serial.println(" };");
}

/**
 * @brief Main function to display system information.
 */
void displaySystemInfo() {
  Serial.println("\n\n==========================================");
  Serial.println("      HARDWARE IDENTITY DIAGNOSTIC        ");
  Serial.println("==========================================");
  
  // 1. Board Model
  Serial.printf("Target Board   : %s\n", getBoardModel());
  
  // 2. Chip ID / Mac (Standard)
  Serial.print("MAC Address    : ");
  Serial.println(WiFi.macAddress());

  Serial.println("------------------------------------------");
  
  // 3. Copy-Paste Friendly Format
  // This is specifically designed for your ESP-NOW code!
  Serial.println(">> COPY THE LINE BELOW FOR YOUR CODE <<");
  printMacAsCArray();
  
  Serial.println("==========================================\n");
}

// =============================================================
// MAIN EXECUTION
// =============================================================

void setup() {
  Serial.begin(SERIAL_BAUD_RATE);
  
  // Wait for serial monitor to catch up
  delay(1000);

  // Set WiFi to Station mode to get the correct permanent MAC
  WiFi.mode(WIFI_STA);

  displaySystemInfo();
}

void loop() {
  // Nothing to do here. 
  // We can re-print every 5 seconds just in case the user missed it.
  delay(5000);
}