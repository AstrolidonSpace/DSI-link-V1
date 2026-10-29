#pragma once

#include <Arduino.h>

/**
 * Initializes the Wi-Fi Access Point and Web Server for live channel & link diagnostics.
 * SoftAP SSID & Password are configured in config.h (default: DSI-LINK-DEBUG / 12345678).
 * 
 * Access the dashboard by connecting to the Wi-Fi AP and opening http://192.168.4.1
 * 
 * @return true if Wi-Fi SoftAP and WebServer started successfully.
 */
bool web_debug_init();

/**
 * Handles incoming HTTP client requests.
 * Call this in loop() when running in TX_MODE_WEB_DEBUG mode.
 */
void web_debug_update();

